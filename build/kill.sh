#!/usr/bin/env sh
ps -ef | grep -i vnote.app | grep -v grep | awk '{print $2}' | xargs kill
