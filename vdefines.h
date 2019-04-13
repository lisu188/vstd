#pragma once

#ifdef Py_PYTHON_H
#define PY_SAFE(x) try{x;}catch(...){vstd::logger::debug();PyErr_Print();PyErr_Clear();}
#define PY_UNSAFE(x) try{x;}catch(...){vstd::logger::debug();PyErr_Print();PyErr_Clear();throw;}
#define PY_SAFE_RET(x) try{x;}catch(...){vstd::logger::debug();PyErr_Print();PyErr_Clear();return nullptr;}
#define PY_SAFE_RET_VAL(x, r) try{x;}catch(...){vstd::logger::debug();PyErr_Print();PyErr_Clear();return r;}
#endif