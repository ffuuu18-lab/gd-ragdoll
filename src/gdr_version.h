// gdr_version.h - the single place the release version is written down.
//
// tools\package.ps1 reads GDR_VERSION out of this file to name the staged folder and the zip, and
// the mod logs it at startup, so a log always says which build produced it.
#pragma once

#define GDR_VERSION "1.0.0"
