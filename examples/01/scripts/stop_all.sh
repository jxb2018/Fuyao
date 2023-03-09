#!/bin/bash
source_dir=/home/lgw
build_dir=/tmp/Fuyao
#build_dir=/tmp/lumine-benchmarks
kill `ps x | grep ${build_dir} | grep -v grep | awk '{print $1}'`