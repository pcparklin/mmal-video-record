Raspberry Pi mmal video record with dynamic overlay(text) rendering project.

This project uses sqlite3 as resource of overlay content.

Build
-----
0. Install pre-required packages
   
    $ sudo apt-get install cmake libcairo2-dev libsqlite3-dev sqlite3
    

1. Build project 

    $ cmake .
    
    $ make 


Using
-----

-b: setting bitrate, default is 1700000.

-f: setting framerate, default is 25.

-F: setting font file path.

-g: setting intraperiod, default is 50.

-h: setting video height, default is 720.

-H: setting font size, default is 25.

-p: enable preview, default is disabled.

-s: setting sqlite3 db path, default is '/dev/shm/web.db'.

-w: setting video width, default is 1280.

-W: setting overlay width, default is 1280.


DB sample
---------

X: INT,
Y: INT,
TEXT: TEXT,
COLOR: CHAR(6),
PY: INT,
PU: INT,
PV: INT,
URL: BLOB,
MODIFIED: CHAR(19)


Sample Video
------------

https://www.youtube.com/watch?v=q50wNNM5whA


Refrence
--------

https://github.com/tasanakorn/rpi-mmal-demo
