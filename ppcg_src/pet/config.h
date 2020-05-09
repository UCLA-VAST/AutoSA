/* config.h.  Generated from config.h.in by configure.  */
/* config.h.in.  Generated from configure.ac by autoheader.  */

/* Define if HeaderSearchOptions::AddPath takes 4 arguments */
#define ADDPATH_TAKES_4_ARGUMENTS /**/

/* Clang installation prefix */
#define CLANG_PREFIX "/usr/lib/llvm-3.8"

/* Define if CompilerInstance::createDiagnostics takes argc and argv */
/* #undef CREATEDIAGNOSTICS_TAKES_ARG */

/* Define if CompilerInstance::createPreprocessor takes TranslationUnitKind */
#define CREATEPREPROCESSOR_TAKES_TUKIND /**/

/* Define if TargetInfo::CreateTargetInfo takes pointer */
/* #undef CREATETARGETINFO_TAKES_POINTER */

/* Define if TargetInfo::CreateTargetInfo takes shared_ptr */
#define CREATETARGETINFO_TAKES_SHARED_PTR /**/

/* Define if CompilerInvocation::CreateFromArgs takes ArrayRef */
/* #undef CREATE_FROM_ARGS_TAKES_ARRAYREF */

/* Define if Driver constructor takes default image name */
/* #undef DRIVER_CTOR_TAKES_DEFAULTIMAGENAME */

/* Define to DiagnosticClient for older versions of clang */
/* #undef DiagnosticConsumer */

/* Define to Diagnostic for newer versions of clang */
#define DiagnosticInfo Diagnostic

/* Define to Diagnostic for older versions of clang */
/* #undef DiagnosticsEngine */

/* Define if getTypeInfo returns TypeInfo object */
#define GETTYPEINFORETURNSTYPEINFO /**/

/* Define if llvm/ADT/OwningPtr.h exists */
/* #undef HAVE_ADT_OWNINGPTR_H */

/* Define if clang/Basic/DiagnosticOptions.h exists */
#define HAVE_BASIC_DIAGNOSTICOPTIONS_H /**/

/* Define if getBeginLoc and getEndLoc should be used */
/* #undef HAVE_BEGIN_END_LOC */

/* Define if clang/Basic/LangStandard.h exists */
/* #undef HAVE_CLANG_BASIC_LANGSTANDARD_H */

/* Define if Driver constructor takes CXXIsProduction argument */
/* #undef HAVE_CXXISPRODUCTION */

/* Define if DecayedType is defined */
#define HAVE_DECAYEDTYPE /**/

/* Define to 1 if you have the <dlfcn.h> header file. */
#define HAVE_DLFCN_H 1

/* Define if SourceManager has findLocationAfterToken method */
#define HAVE_FINDLOCATIONAFTERTOKEN /**/

/* Define to 1 if you have the <inttypes.h> header file. */
#define HAVE_INTTYPES_H 1

/* Define if Driver constructor takes IsProduction argument */
/* #undef HAVE_ISPRODUCTION */

/* Define if clang/Lex/HeaderSearchOptions.h exists */
#define HAVE_LEX_HEADERSEARCHOPTIONS_H /**/

/* Define if clang/Lex/PreprocessorOptions.h exists */
#define HAVE_LEX_PREPROCESSOROPTIONS_H /**/

/* Define if llvm/Option/Arg.h exists */
#define HAVE_LLVM_OPTION_ARG_H /**/

/* Define to 1 if you have the <memory.h> header file. */
#define HAVE_MEMORY_H 1

/* Define if SourceManager has a setMainFileID method */
#define HAVE_SETMAINFILEID /**/

/* Define to 1 if you have the <stdint.h> header file. */
#define HAVE_STDINT_H 1

/* Define to 1 if you have the <stdlib.h> header file. */
#define HAVE_STDLIB_H 1

/* Define if StmtRange class is available */
/* #undef HAVE_STMTRANGE */

/* Define to 1 if you have the <strings.h> header file. */
#define HAVE_STRINGS_H 1

/* Define to 1 if you have the <string.h> header file. */
#define HAVE_STRING_H 1

/* Define to 1 if you have the <sys/stat.h> header file. */
#define HAVE_SYS_STAT_H 1

/* Define to 1 if you have the <sys/types.h> header file. */
#define HAVE_SYS_TYPES_H 1

/* Define if SourceManager has translateLineCol method */
#define HAVE_TRANSLATELINECOL /**/

/* Define to 1 if you have the <unistd.h> header file. */
#define HAVE_UNISTD_H 1

/* Return type of HandleTopLevelDeclReturn */
#define HandleTopLevelDeclContinue true

/* Return type of HandleTopLevelDeclReturn */
#define HandleTopLevelDeclReturn bool

/* Define to Language::C or InputKind::C for newer versions of clang */
/* #undef IK_C */

/* Define to the sub-directory where libtool stores uninstalled libraries. */
#define LT_OBJDIR ".libs/"

/* Name of package */
#define PACKAGE "pet"

/* Define to the address where bug reports for this package should be sent. */
#define PACKAGE_BUGREPORT "isl-development@googlegroups.com"

/* Define to the full name of this package. */
#define PACKAGE_NAME "pet"

/* Define to the full name and version of this package. */
#define PACKAGE_STRING "pet 0.11.3"

/* Define to the one symbol short name of this package. */
#define PACKAGE_TARNAME "pet"

/* Define to the home page for this package. */
#define PACKAGE_URL ""

/* Define to the version of this package. */
#define PACKAGE_VERSION "0.11.3"

/* Define to PragmaIntroducerKind for older versions of clang */
#define PragmaIntroducer PragmaIntroducerKind

/* Defined if CompilerInstance::setInvocation takes a shared_ptr */
/* #undef SETINVOCATION_TAKES_SHARED_PTR */

/* Define if CompilerInvocation::setLangDefaults takes 5 arguments */
/* #undef SETLANGDEFAULTS_TAKES_5_ARGUMENTS */

/* Define to 1 if you have the ANSI C header files. */
#define STDC_HEADERS 1

/* Define to TypedefDecl for older versions of clang */
/* #undef TypedefNameDecl */

/* Define if Driver::BuildCompilation takes ArrayRef */
#define USE_ARRAYREF /**/

/* Version number of package */
#define VERSION "0.11.3"

/* Define to getHostTriple for older versions of clang */
/* #undef getDefaultTargetTriple */

/* Define to getInstantiationColumnNumber for older versions of clang */
/* #undef getExpansionColumnNumber */

/* Define to getInstantiationLineNumber for older versions of clang */
/* #undef getExpansionLineNumber */

/* Define to getInstantiationLoc for older versions of clang */
/* #undef getExpansionLoc */

/* Define to getLangOptions for older versions of clang */
/* #undef getLangOpts */

/* Define to getFileLocWithOffset for older versions of clang */
/* #undef getLocWithOffset */

/* Define to getResultType for older versions of clang */
/* #undef getReturnType */

/* Define to getTypedefForAnonDecl for older versions of clang */
/* #undef getTypedefNameForAnonDecl */

/* Define to InitializeBuiltins for older versions of clang */
/* #undef initializeBuiltins */
