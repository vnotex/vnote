# Localization and Translations
For now, VNote only supports English and Simplified Chinese as interface's language. A Japanese translation is provided by VNote's users. It is highly appreciated to help us translate VNote to other languages.

To provide translations for VNote, you only need to download VNote's source code and get Qt installed. You do not need the environment to build VNote. What we need at last for a new locale are just two files: `*.ts` and `*.qm`.

## Tools from Qt
After installing Qt, there are two tools we need in `path_to_Qt/bin` folder: `lupdate` and `linguist`.

## Add ts file
Given that we want to provide a Traditional Chinese translation for VNote. The locale code should be `zh_HK`. First we add one empty plain text file `src/data/core/translations/vnote_zh_HK.ts` under VNote's source code.

Then add it to `src/src.pro` file:

```pro
TRANSLATIONS += data/core/translations/vnote_zh_CN.ts \
                data/core/translations/vnote_ja.ts \
                data/core/translations/vnote_zh_HK.ts
```

## Update ts file
Now we need to update `vnote_zh_HK.ts` according to latest source code via `lupdate` tool.

Execute `lupdate` update VNote's source code root folder:

```
lupdate src/src.pro
```

It will fill in `vnote_zh_HK.ts` with latest strings needed to translate.

## Fill in ts file with translations
```
linguist src/data/core/translations/vnote_zh_HK.ts
```

This will open Linguist tool for us to do the translation one by one.

After doing all the translations, select `Save` and `Release` in the `File` menu of Linguist. The `vnote_zh_HK.qm` file should now be generated alongside with the `ts` file.

## Submit files
Now you could submit those two files to the author. The author will integrate them in next release of VNote.

## Update after changes
You need to update the `ts` and `qm` file if there are changes in source code via `lupdate` and `linguist`.