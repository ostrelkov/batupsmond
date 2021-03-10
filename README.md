# batupsmond

## Battery UPS Monitor Daemon 

The daemon is useful for computer systems with a battery as a UPS (for example, ARM systems).  
The daemon monitors the state of external power through the *sysfs* file system.  
If external power is lost and the system starts to operate on battery power, then the daemon runs an external `pwrdown` script after a specified timeout to shutdown the system.  
If power is restored during the timeout (false alarm), then daemon continues monitoring.  

## Usage

```
# batupsmond [-n] [-f sec]
-n - nodaemon
-h - help
-f sec - false alarm duration in seconds
```

Example `pwrdown` script:

```
#!/bin/sh
# you can run something before powering off
poweroff
```

Daemon is tested on boards:

* Olimex A10/A20-OLinuXino-LIME
* Olimex  A20-OLinuXino-LIME2
* Olimex A20-SOM204-EVB

with kernel 3.4.x.
