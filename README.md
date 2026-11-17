# Planetary Hour Clock

A clock that shows the planetary hour, the day of the week, and the old unequal
(temporal) hours of day and night, all on one printed dial that stays correct
all year. Everything that changes with the seasons is handled by *speed*, not
drawing: the hands and LEDs speed up and slow down, so sunrise is always at the
same place on the dial.

- Two mechanical hands (temporal hour hand, gold planetary-hour hand)
- 84-LED day ring plus 18 LEDs on the star and compass points
- A 46 mm round AMOLED in the centre showing civil time and the current hour's length

## Hardware

- Controller and centre screen: Waveshare ESP32-S3-Touch-AMOLED-1.43 (466×466 round AMOLED)

## Layout

- `firmware/`: ESP32 code (PlatformIO); see `firmware/README.md`
