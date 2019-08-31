#!/bin/bash
if [ "$#" -lt 1 ]; then
	echo "usage: GenerateFont.sh <font path>"
	exit
fi

FILE_NAME="font.ttf"

if [ ${1: -4} == ".otf" ]; then
	fontforge -lang=ff -c 'Open($1); Generate($2)' $1 $FILE_NAME 2> /dev/null
else
	cp $1 $FILE_NAME
fi

xxd -i $FILE_NAME > Font.h
rm $FILE_NAME
