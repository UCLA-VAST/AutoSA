#!/bin/sh
git submodule init
git submodule update
(cd src/isl; git submodule init imath; git submodule update imath)
