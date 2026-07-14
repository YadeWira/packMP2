@ECHO OFF
SETLOCAL

SET INPUT="%~f1"
SET OUTPUT="%~f1.mp2"

lpaq8 d %INPUT% - | unpackmp2 p > %OUTPUT%
