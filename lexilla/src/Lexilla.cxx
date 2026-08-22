// Lexilla source code
#include <cstddef>
#include <cstdlib>
#include <cstring>
#include <cstdio>
#include <cassert>
#include <algorithm>

#include "ILexer.h"
#include "Scintilla.h"
#include "SciLexer.h"

#include "PropSetSimple.h"
#include "WordList.h"
#include "LexAccessor.h"
#include "Accessor.h"
#include "StyleContext.h"
#include "CharacterSet.h"
#include "LexerModule.h"
#include "LexerBase.h"
#include "LexerNoExceptions.h"

#include "Lexilla.h"

// Include all the lexers
#include "LexA68k.cxx"
#include "LexAbaqus.cxx"
#include "LexAda.cxx"
#include "LexAPDL.cxx"
#include "LexAsm.cxx"
#include "LexAsymptote.cxx"
#include "LexAutoHotKey.cxx"
#include "LexAutoIt3.cxx"
#include "LexAVS.cxx"
#include "LexBaan.cxx"
#include "LexBash.cxx"
#include "LexBatch.cxx"
#include "LexBibTeX.cxx"
#include "LexBLITZBasic.cxx"
#include "LexBullant.cxx"
#include "LexCaml.cxx"
#include "LexCIL.cxx"
#include "LexClw.cxx"
#include "LexCmake.cxx"
#include "LexCOBOL.cxx"
#include "LexCoffeeScript.cxx"
#include "LexConf.cxx"
#include "LexCPP.cxx"
#include "LexCrontab.cxx"
#include "LexCsound.cxx"
#include "LexCSS.cxx"
#include "LexD.cxx"
#include "LexDataflex.cxx"
#include "LexDiff.cxx"
#include "LexDMIS.cxx"
#include "LexECL.cxx"
#include "LexEiffel.cxx"
#include "LexEiffelkw.cxx"
#include "LexElixir.cxx"
#include "LexErlang.cxx"
#include "LexErrorList.cxx"
#include "LexEScript.cxx"
#include "LexFlagship.cxx"
#include "LexForth.cxx"
#include "LexFortran.cxx"
#include "LexGAP.cxx"
#include "LexGDScript.cxx"
#include "LexGui4Cli.cxx"
#include "LexHaskell.cxx"
#include "LexHex.cxx"
#include "LexHollywood.cxx"
#include "LexHTML.cxx"
#include "LexInno.cxx"
#include "LexJSON.cxx"
#include "LexJulia.cxx"
#include "LexKix.cxx"
#include "LexKotlin.cxx"
#include "LexLaTeX.cxx"
#include "LexLisp.cxx"
#include "LexLout.cxx"
#include "LexLua.cxx"
#include "LexMagik.cxx"
#include "LexMake.cxx"
#include "LexMarkdown.cxx"
#include "LexMatlab.cxx"
#include "LexMaxima.cxx"
#include "LexMetapost.cxx"
#include "LexMMIXAL.cxx"
#include "LexModula.cxx"
#include "LexMPT.cxx"
#include "LexMSSQL.cxx"
#include "LexMySQL.cxx"
#include "LexNim.cxx"
#include "LexNsis.cxx"
#include "LexNull.cxx"
#include "LexOberon.cxx"
#include "LexObjC.cxx"
#include "LexOCaml.cxx"
#include "LexOpal.cxx"
#include "LexOScript.cxx"
#include "LexPascal.cxx"
#include "LexPB.cxx"
#include "LexPerl.cxx"
#include "LexPHP.cxx"
#include "LexPLM.cxx"
#include "LexPOV.cxx"
#include "LexPowerPro.cxx"
#include "LexPowerShell.cxx"
#include "LexProgress.cxx"
#include "LexProps.cxx"
#include "LexPS.cxx"
#include "LexPython.cxx"
#include "LexR.cxx"
#include "LexRaku.cxx"
#include "LexRebol.cxx"
#include "LexRegistry.cxx"
#include "LexRenPy.cxx"
#include "LexRust.cxx"
#include "LexSAS.cxx"
#include "LexScriptol.cxx"
#include "LexShaders.cxx"
#include "LexSmalltalk.cxx"
#include "LexSML.cxx"
#include "LexSorcus.cxx"
#include "LexSpecman.cxx"
#include "LexSpice.cxx"
#include "LexSQL.cxx"
#include "LexStata.cxx"
#include "LexSTL.cxx"
#include "LexTACL.cxx"
#include "LexTCL.cxx"
#include "LexTCMD.cxx"
#include "LexTeX.cxx"
#include "LexTxt2tags.cxx"
#include "LexVB.cxx"
#include "LexVBA.cxx"
#include "LexVerilog.cxx"
#include "LexVHDL.cxx"
#include "LexVisualProlog.cxx"
#include "LexX12.cxx"
#include "LexYAML.cxx"

// Windows-specific lexer - only include on Windows
#ifdef _WIN32
#include "LexUser.cxx"
#endif

using namespace Scintilla;

// Define the lexer registry
static const LexerModule *lexerModules[] = {
    &lmA68k,
    &lmAbaqus,
    &lmAda,
    &lmAPDL,
    &lmAsm,
    &lmAsymptote,
    &lmAutoHotKey,
    &lmAutoIt3,
    &lmAVS,
    &lmBaan,
    &lmBash,
    &lmBatch,
    &lmBibTeX,
    &lmBLITZBasic,
    &lmBullant,
    &lmCaml,
    &lmCIL,
    &lmClw,
    &lmCmake,
    &lmCOBOL,
    &lmCoffeeScript,
    &lmConf,
    &lmCPP,
    &lmCrontab,
    &lmCsound,
    &lmCSS,
    &lmD,
    &lmDataflex,
    &lmDiff,
    &lmDMIS,
    &lmECL,
    &lmEiffel,
    &lmEiffelkw,
    &lmElixir,
    &lmErlang,
    &lmErrorList,
    &lmEScript,
    &lmFlagship,
    &lmForth,
    &lmFortran,
    &lmGAP,
    &lmGDScript,
    &lmGui4Cli,
    &lmHaskell,
    &lmHex,
    &lmHollywood,
    &lmHTML,
    &lmInno,
    &lmJSON,
    &lmJulia,
    &lmKix,
    &lmKotlin,
    &lmLaTeX,
    &lmLisp,
    &lmLout,
    &lmLua,
    &lmMagik,
    &lmMake,
    &lmMarkdown,
    &lmMatlab,
    &lmMaxima,
    &lmMetapost,
    &lmMMIXAL,
    &lmModula,
    &lmMPT,
    &lmMSSQL,
    &lmMySQL,
    &lmNim,
    &lmNsis,
    &lmNull,
    &lmOberon,
    &lmObjC,
    &lmOCaml,
    &lmOpal,
    &lmOScript,
    &lmPascal,
    &lmPB,
    &lmPerl,
    &lmPHP,
    &lmPLM,
    &lmPOV,
    &lmPowerPro,
    &lmPowerShell,
    &lmProgress,
    &lmProps,
    &lmPS,
    &lmPython,
    &lmR,
    &lmRaku,
    &lmRebol,
    &lmRegistry,
    &lmRenPy,
    &lmRust,
    &lmSAS,
    &lmScriptol,
    &lmShaders,
    &lmSmalltalk,
    &lmSML,
    &lmSorcus,
    &lmSpecman,
    &lmSpice,
    &lmSQL,
    &lmStata,
    &lmSTL,
    &lmTACL,
    &lmTCL,
    &lmTCMD,
    &lmTeX,
    &lmTxt2tags,
    &lmVB,
    &lmVBA,
    &lmVerilog,
    &lmVHDL,
    &lmVisualProlog,
    &lmX12,
    &lmYAML,

    // Windows-specific lexer module - only include on Windows
#ifdef _WIN32
    &lmUserDefine,
#endif

    nullptr
};

// Create a lexer factory function
extern "C" SCLEXPORT int CreateLexer(const char *name) {
    if (!name)
        return -1;
    
    for (const LexerModule **module = lexerModules; *module; module++) {
        if ((*module)->GetName() && strcmp((*module)->GetName(), name) == 0) {
            return (*module)->GetID();
        }
    }
    return -1;
}

// Create a lexer instance
extern "C" SCLEXPORT ILexer5* CreateLexerInstance(int id) {
    for (const LexerModule **module = lexerModules; *module; module++) {
        if ((*module)->GetID() == id) {
            return (*module)->Create();
        }
    }
    return nullptr;
}

// Get the number of lexers
extern "C" SCLEXPORT int GetLexerCount() {
    int count = 0;
    for (const LexerModule **module = lexerModules; *module; module++) {
        count++;
    }
    return count;
}

// Get a lexer by index
extern "C" SCLEXPORT const char* GetLexerName(int index) {
    if (index < 0)
        return nullptr;
    
    int i = 0;
    for (const LexerModule **module = lexerModules; *module; module++) {
        if (i == index) {
            return (*module)->GetName();
        }
        i++;
    }
    return nullptr;
}

// Get a lexer ID by name
extern "C" SCLEXPORT int GetLexerID(const char* name) {
    if (!name)
        return -1;
    
    for (const LexerModule **module = lexerModules; *module; module++) {
        if ((*module)->GetName() && strcmp((*module)->GetName(), name) == 0) {
            return (*module)->GetID();
        }
    }
    return -1;
}

// Get a lexer by ID
extern "C" SCLEXPORT const LexerModule* GetLexerModule(int id) {
    for (const LexerModule **module = lexerModules; *module; module++) {
        if ((*module)->GetID() == id) {
            return *module;
        }
    }
    return nullptr;
}
