#!/bin/sh
echo #define THREADNETPERF_VERSION \" > version.h
svnversion -n . >> version.h
echo \">> version.h