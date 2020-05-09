#!/bin/sh
git submodule init
git submodule update
(cd isl; git submodule init imath; git submodule update imath)
