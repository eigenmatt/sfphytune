### sfphytune
SERDES tuning &amp; eye scan utility for Solarflare network adapters

Display receive equalization settings:

```sh
sfphytune ethX rxeq
```

Display transmit equalisation settings:

```sh
sfphytune ethX txeq
```

Set a setting to a fixed value:

```sh
sfphytune ethX rxeq Lane0.Attenuation=5
```

Set initial value, enabling autocalibration (valid for rxeq only):

```sh
sfphytune ethX rxeq Lane0.Attenuation=5+
```

Trigger recalibration:

```sh
sfphytune ethX calibrate
```

Eye scan:

```sh
sfphytune ethX eye >eye.dat
```

