@ECHO OFF
SETLOCAL

SET INPUT="%~f1"
SET OUTPUT="%~dpn1.um2.lpaq8"

unpackmp2 u < %INPUT% | lpaq8 5 - %OUTPUT%
