/*
 * SERDES tuning & eye scan utility for Solarflare network adapters
 * Copyright 2015 Matthew Chapman <matthew.chapman@exablaze.com>
 */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdint.h>
#include <unistd.h>
#include <errno.h>
#include <endian.h>
#include <sys/ioctl.h>
#include <efx_ioctl.h>
#include <mcdi_pcol.h>
#include <bitfield.h>

const char *rxeq_param[] = {
    "Attenuation",
    "CTLE_Boost",
    "DFE_Tap1", 
    "DFE_Tap2", 
    "DFE_Tap3", 
    "DFE_Tap4", 
    "DFE_Tap5", 
    "DFE_Gain"
};

const char *txeq_param[] = {
    "Amplitude",
    "Deemphasis_Tap1",
    "Deemphasis_Tap1_Fine",
    "Deemphasis_Tap2",
    "Deemphasis_Tap2_Fine",
    "Preemphasis",
    "Preemphasis_Fine",
    "Slew_Rate",
    "Slew_Rate_Fine",
    "Termination"
};

#define rxeq_param_count (sizeof(rxeq_param)/sizeof(rxeq_param[0]))
#define txeq_param_count (sizeof(txeq_param)/sizeof(txeq_param[0]))

static void show_param(uint32_t *buf, size_t len,
        const char **param_names, size_t param_count)
{
    size_t i;
    uint32_t val;
    uint8_t param, lane, autocal, initial, current;
    const char *label;

    for (i = 0; i < len/4; i++)
    {
        val = le32toh(buf[i]);
        param = val & 0xff;
        lane = (val >> 8) & 7;
        autocal = (val >> 11) & 1;
        initial = (val >> 16) & 0xff;
        current = (val >> 24) & 0xff;
        if (param >= param_count)
        {
            printf("unknown parameter %u\n", param);
            continue;
        }
        label = param_names[param];
        if (autocal)
            printf("Lane%u.%s=%u (initial=%u)\n", lane, label, current, initial);
        else
            printf("Lane%u.%s=%u\n", lane, label, initial);
    }
}

static int set_param(char *desc, uint32_t *outval,
        const char **param_names, size_t param_count)
{
    char *label, *valp;
    uint32_t lane, autocal, val;
    size_t i;

    if (strncmp(desc, "Lane", 4) != 0)
        return 0;
    if (desc[4] < '0' || desc[4] > '4')
        return 0;
    lane = desc[4] - '0';
    if (desc[5] != '.')
        return 0;

    label = &desc[6];
    valp = strchr(label, '=');
    if (valp == NULL)
        return 0;
    *valp = 0;
    val = strtoul(valp+1, &valp, 0);
    autocal = (*valp == '+');
    if (autocal)
        valp++;
    if (*valp != 0)
        return 0;

    for (i = 0; i < param_count; i++)
    {
        if (!strcmp(param_names[i], label))
        {
            *outval = htole32(i|(lane<<8)|(autocal<<11)|(val<<16));
            return 1;
        }
    }

    return 0;
}

int efx_mcdi_rpc(const char *ifname, unsigned cmd,
                 const uint32_t *inbuf, size_t inlen,
                 uint32_t *outbuf, size_t outlen,
                 size_t *outlen_actual)
{
    struct efx_sock_ioctl efx;
    struct ifreq ifr;
    size_t bufsize;
    int fd, rc;

    bufsize = sizeof(efx)-((char *)efx.u.mcdi_request2.payload-(char *)&efx);
    if ((inlen > bufsize) || (outlen > bufsize))
    {
        errno = ENOMEM;
        return -1;
    }

    fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (fd == -1)
        return -1;

    memset(&efx, 0, sizeof(efx));
    efx.cmd = EFX_MCDI_REQUEST2;
    efx.u.mcdi_request2.cmd = cmd;
    efx.u.mcdi_request2.inlen = inlen;
    efx.u.mcdi_request2.outlen = outlen;
    memcpy(efx.u.mcdi_request2.payload, inbuf, inlen);

    strncpy(ifr.ifr_name, ifname, sizeof(ifr.ifr_name));
    ifr.ifr_data = (caddr_t)&efx;
    rc = ioctl(fd, SIOCEFX, &ifr);
    if (rc)
        goto sys_error;
    if (efx.u.mcdi_request2.flags & EFX_MCDI_REQUEST_ERROR)
        goto mcdi_error;

    if (outlen_actual)
        *outlen_actual = efx.u.mcdi_request2.outlen;
    memcpy(outbuf, efx.u.mcdi_request2.payload, efx.u.mcdi_request2.outlen);
    return 0;

mcdi_error:
    errno = efx.u.mcdi_request2.host_errno;
sys_error:
    close(fd);
    return -1;
}

int efx_get_rxeq(const char *ifname)
{
    uint32_t inbuf[1];
    uint32_t outbuf[MC_CMD_KR_TUNE_RXEQ_GET_OUT_LENMAX/4];
    size_t outlen;
    int rc;

    inbuf[0] = htole32(MC_CMD_KR_TUNE_IN_RXEQ_GET);
    rc = efx_mcdi_rpc(ifname, MC_CMD_KR_TUNE, inbuf, 4, outbuf, sizeof(outbuf), &outlen);
    if (rc)
    {
        perror("MC_CMD_KR_TUNE_IN_RXEQ_GET");
        return 1;
    }

    show_param(outbuf, outlen, rxeq_param, rxeq_param_count);
    return 0;
}

int efx_set_rxeq(const char *ifname, char *desc)
{
    uint32_t inbuf[2];
    int rc;

    inbuf[0] = htole32(MC_CMD_KR_TUNE_IN_RXEQ_SET);
    if (!set_param(desc, &inbuf[1], rxeq_param, rxeq_param_count))
    {
        fprintf(stderr, "parse error\n");
        return 1;
    }
    rc = efx_mcdi_rpc(ifname, MC_CMD_KR_TUNE, inbuf, 8, NULL, 0, NULL);
    if (rc)
    {
        perror("MC_CMD_KR_TUNE_IN_RXEQ_SET");
        return 1;
    }
    return 0;
}

int efx_get_txeq(const char *ifname)
{
    uint32_t inbuf[1];
    uint32_t outbuf[MC_CMD_KR_TUNE_TXEQ_GET_OUT_LENMAX/4];
    size_t outlen;
    int rc;

    inbuf[0] = htole32(MC_CMD_KR_TUNE_IN_TXEQ_GET);
    rc = efx_mcdi_rpc(ifname, MC_CMD_KR_TUNE, inbuf, 4, outbuf, sizeof(outbuf), &outlen);
    if (rc)
    {
        perror("MC_CMD_KR_TUNE_IN_TXEQ_GET");
        return 1;
    }

    show_param(outbuf, outlen, txeq_param, txeq_param_count);
    return 0;
}

int efx_set_txeq(const char *ifname, char *desc)
{
    uint32_t inbuf[2];
    int rc;

    inbuf[0] = htole32(MC_CMD_KR_TUNE_IN_TXEQ_SET);
    if (!set_param(desc, &inbuf[1], txeq_param, txeq_param_count))
    {
        fprintf(stderr, "parse error\n");
        return 1;
    }
    rc = efx_mcdi_rpc(ifname, MC_CMD_KR_TUNE, inbuf, 8, NULL, 0, NULL);
    if (rc)
    {
        perror("MC_CMD_KR_TUNE_IN_TXEQ_SET");
        return 1;
    }
    return 0;
}

int efx_calibrate(const char *ifname)
{
    uint32_t inbuf[1];
    int rc;

    inbuf[0] = htole32(MC_CMD_KR_TUNE_IN_RECAL);
    rc = efx_mcdi_rpc(ifname, MC_CMD_KR_TUNE, inbuf, 4, NULL, 0, NULL);
    if (rc)
    {
        perror("MC_CMD_KR_TUNE_IN_RECAL");
        return 1;
    }
    return 0;
}

int efx_get_lane(const char *ifname, uint32_t *lane)
{
    uint32_t inbuf[1];
    uint32_t outbuf[MC_CMD_KR_TUNE_RXEQ_GET_OUT_LENMAX/4];
    size_t outlen;
    int rc;

    inbuf[0] = htole32(MC_CMD_KR_TUNE_IN_RXEQ_GET);
    rc = efx_mcdi_rpc(ifname, MC_CMD_KR_TUNE, inbuf, 4, outbuf, sizeof(outbuf), &outlen);
    if (rc)
    {
        perror("MC_CMD_KR_TUNE_IN_RXEQ_GET");
        return 1;
    }

    *lane = (outbuf[0] >> 8) & 7;
    return 0;
}

int efx_get_eye(const char *ifname)
{
    uint32_t inbuf[2];
    uint32_t outbuf[MC_CMD_KR_TUNE_POLL_EYE_PLOT_OUT_LENMAX/4];
    uint32_t val;
    size_t outlen, i;
    int rc;

    inbuf[0] = htole32(MC_CMD_KR_TUNE_IN_START_EYE_PLOT);
    if (efx_get_lane(ifname, &val) != 0)
        return 1;
    inbuf[1] = htole32(val);
    rc = efx_mcdi_rpc(ifname, MC_CMD_KR_TUNE, inbuf, 8, NULL, 0, NULL);
    if (rc)
    {
        perror("MC_CMD_KR_TUNE_IN_START_EYE_PLOT");
        return 1;
    }

    while (1)
    {
        inbuf[0] = htole32(MC_CMD_KR_TUNE_IN_POLL_EYE_PLOT);
        rc = efx_mcdi_rpc(ifname, MC_CMD_KR_TUNE, inbuf, 4, outbuf, sizeof(outbuf), &outlen);
        if (rc)
        {
            perror("MC_CMD_KR_TUNE_IN_POLL_EYE_PLOT");
            return 1;
        }

        if (outlen == 0)
            break;

        for (i = 0; i < outlen/4; i++)
        {
            val = le32toh(outbuf[i]);
            printf("%u %u ", outbuf[i] & 0xffff, (outbuf[i] >> 16) & 0xffff);
        }
        printf("\n");
    }
    return 0;
}

int main(int argc, char *argv[])
{
    const char *ifname, *command;

    if ((argc < 3) || (argc > 4))
        goto usage;

    ifname = argv[1];
    command = argv[2];

    if (strcmp(command, "rxeq") == 0)
    {
        if (argc == 4)
            return efx_set_rxeq(ifname, argv[3]);
        else
            return efx_get_rxeq(ifname);
    }
    else if (strcmp(command, "txeq") == 0)
    {
        if (argc == 4)
            return efx_set_txeq(ifname, argv[3]);
        else
            return efx_get_txeq(ifname);
    }
    else if (strcmp(command, "calibrate") == 0)
    {
        return efx_calibrate(ifname);
    }
    else if (strcmp(command, "eye") == 0)
    {
        return efx_get_eye(ifname);
    }

usage:
    fprintf(stderr, "usage: %s ifname {rxeq|txeq|calibrate|eye} [args]\n", argv[0]);
    return 1;
}

