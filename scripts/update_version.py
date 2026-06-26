import fileinput
import sys
import re
import datetime

if len(sys.argv) < 2:
    print("Please provide a new version string!")
    exit

newVersion = sys.argv[1]
shortVersion = re.match('^(\\d+\\.\\d+).', newVersion).group(1)
print("New version: {0}".format(newVersion))

# CMakeList
regExp = re.compile('(\\s+)VERSION \\S+')
for line in fileinput.input(['CMakeLists.txt'], inplace = True):
    print(regExp.sub('\\1VERSION ' + newVersion, line), end='')

# ci-xxx.yml
regExp = re.compile('(\\s+)VNOTE_VER: \\S+')
for line in fileinput.input(['.github/workflows/ci-win.yml', '.github/workflows/ci-linux.yml', '.github/workflows/ci-macos.yml'], inplace = True):
    print(regExp.sub('\\1VNOTE_VER: ' + newVersion, line), end='')

# Info.plist
regExp = re.compile('(\\s+)<string>(?!10\\.15)\\d+\\.\\d+</string>')
for line in fileinput.input(['src/data/core/Info.plist'], inplace = True):
    print(regExp.sub('\\1<string>' + shortVersion + '</string>', line), end='')

regExp = re.compile('(\\s+)<string>\\d+\\.\\d+\\.\\d+</string>')
for line in fileinput.input(['src/data/core/Info.plist'], inplace = True):
    print(regExp.sub('\\1<string>' + newVersion + '</string>', line), end='')

regExp = re.compile('(\\s+)<string>\\d+\\.\\d+\\.\\d+\\.\\d+</string>')
for line in fileinput.input(['src/data/core/Info.plist'], inplace = True):
    print(regExp.sub('\\1<string>' + newVersion + '.1</string>', line), end='')

# ConfigMgr2.cpp
versionParts = newVersion.replace('.', ', ')
regExp = re.compile('(const QVersionNumber ConfigMgr2::c_version)\\{\\d+, \\d+, \\d+\\}')
for line in fileinput.input(['src/core/configmgr2.cpp'], inplace = True):
    print(regExp.sub('\\1{' + versionParts + '}', line), end='')

# AppStream metainfo: prepend a <release> entry to the <releases> list.
metainfoPath = 'src/data/core/fun.vnote.app.VNote.metainfo.xml'
today = datetime.date.today().isoformat()
newReleaseLine = '      <release version="{0}" date="{1}" />\n'.format(newVersion, today)
releaseLineRe = re.compile('^\\s*<release version="([^"]+)"')
for line in fileinput.input([metainfoPath], inplace = True):
    # Drop any pre-existing entry for this version so re-runs stay idempotent.
    match = releaseLineRe.match(line)
    if match and match.group(1) == newVersion:
        continue
    print(line, end='')
    if '<releases>' in line:
        print(newReleaseLine, end='')
