import fileinput
import sys
import re

if len(sys.argv) < 2:
    print("Please provide a new version string!")
    exit

newVersion = sys.argv[1]
print("New version: {0}".format(newVersion))

# vnotex.json
regExp = re.compile('(\\s+)"version" : "\\S+"')
for line in fileinput.input(['src/data/core/vnotex.json'], inplace = True):
    print(regExp.sub('\\1"version" : "' + newVersion + '"', line), end='')

# ci-xxx.yml
regExp = re.compile('(\\s+)VNOTE_VER: \\S+')
for line in fileinput.input(['.github/workflows/ci-win.yml', '.github/workflows/ci-linux.yml', '.github/workflows/ci-macos.yml'], inplace = True):
    print(regExp.sub('\\1VNOTE_VER: ' + newVersion, line), end='')
