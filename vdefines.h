#pragma once

#ifdef DEBUG_MODE
#define force_inline
#else
#define force_inline __attribute__((always_inline))
#endif

#ifdef Py_PYTHON_H
#define PY_SAFE(x) try{x}catch(...){vstd::logger::debug();PyErr_Print();PyErr_Clear();}
#define PY_SAFE_RET(x) try{x}catch(...){vstd::logger::debug();PyErr_Print();PyErr_Clear();return nullptr;}
#endif