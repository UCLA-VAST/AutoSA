#ifndef PET_CLANG_COMPATIBILITY_H
#define PET_CLANG_COMPATIBILITY_H

#include "config.h"

#ifdef HAVE_BEGIN_END_LOC
template <typename T>
inline clang::SourceLocation begin_loc(T *decl)
{
	return decl->getBeginLoc();
}
template <typename T>
inline clang::SourceLocation end_loc(T *decl)
{
	return decl->getEndLoc();
}
#else
template <typename T>
inline clang::SourceLocation begin_loc(T *decl)
{
	return decl->getLocStart();
}
template <typename T>
inline clang::SourceLocation end_loc(T *decl)
{
	return decl->getLocEnd();
}
#endif

#endif
