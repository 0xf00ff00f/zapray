#!/bin/bash
for i in *ppm; do convert $i -transparent black png32:$( echo $i | sed 's/ppm/png/' ); done
