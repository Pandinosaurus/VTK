// SPDX-FileCopyrightText: Copyright (c) Ken Martin, Will Schroeder, Bill Lorensen
// SPDX-License-Identifier: BSD-3-Clause

#include "vtkWrapPythonMethodDef.h"
#include "vtkWrapPythonClass.h"
#include "vtkWrapPythonMethod.h"
#include "vtkWrapPythonOverload.h"

#include "vtkParseExtras.h"
#include "vtkWrap.h"
#include "vtkWrapText.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* -------------------------------------------------------------------- */
/* prototypes for the methods used by the python wrappers */

/* output the MethodDef table for this class */
static void vtkWrapPython_ClassMethodDef(FILE* fp, const char* classname, const ClassInfo* data,
  WrappedFunction* wrappedFunctions, int numberOfWrappedFunctions, int fnum);

/* print out any custom methods */
static void vtkWrapPython_CustomMethods(
  FILE* fp, const char* classname, ClassInfo* data, int do_constructors);

/* replace AddObserver with a python-specific version */
static void vtkWrapPython_ReplaceAddObserver(FILE* fp, const char* classname, ClassInfo* data);

/* replace InvokeEvent with a python-specific version */
static void vtkWrapPython_ReplaceInvokeEvent(FILE* fp, const char* classname, ClassInfo* data);

/* modify vtkObjectBase methods as needed for Python */
static void vtkWrapPython_ObjectBaseMethods(FILE* fp, const char* classname, ClassInfo* data);

/* modify vtkCollection to be more Pythonic */
static void vtkWrapPython_CollectionMethods(FILE* fp, const char* classname, const ClassInfo* data);

/* modify vtkAlgorithm to be more Pythonic */
static void vtkWrapPython_AlgorithmMethods(FILE* fp, const char* classname, const ClassInfo* data);

/* -------------------------------------------------------------------- */
/* prototypes for utility methods */

/* check for wrappability, flags may be VTK_WRAP_ARG or VTK_WRAP_RETURN */
static int vtkWrapPython_IsValueWrappable(
  const ClassInfo* data, const ValueInfo* val, const HierarchyInfo* hinfo, int flags);

/* weed out methods that will never be called */
static void vtkWrapPython_RemovePrecededMethods(
  WrappedFunction wrappedFunctions[], int numberOfWrappedFunctions, int fnum);

/* -------------------------------------------------------------------- */
/* Check for type precedence. Some method signatures will just never
 * be called because of the way python types map to C++ types.  If
 * we don't remove such methods, they can lead to ambiguities later.
 *
 * The precedence rule is the following:
 * The type closest to the native Python type wins.
 */

static void vtkWrapPython_RemovePrecededMethods(
  WrappedFunction wrappedFunctions[], int numberOfWrappedFunctions, int fnum)
{
  const FunctionInfo* theFunc = wrappedFunctions[fnum].Archetype;
  const char* name;
  FunctionInfo* sig1;
  FunctionInfo* sig2;
  ValueInfo* val1;
  ValueInfo* val2;
  int dim1, dim2;
  int vote1 = 0;
  int vote2 = 0;
  int occ1, occ2;
  unsigned int baseType1, baseType2;
  unsigned int unsigned1, unsigned2;
  unsigned int indirect1, indirect2;
  int i, nargs1, nargs2;
  int argmatch, allmatch;

  if (theFunc == NULL)
  {
    return;
  }

  name = theFunc->Name;
  for (occ1 = fnum; occ1 < numberOfWrappedFunctions; occ1++)
  {
    sig1 = wrappedFunctions[occ1].Archetype;
    if (sig1 && strcmp(sig1->Name, name) == 0)
    {
      nargs1 = vtkWrap_CountWrappedParameters(sig1);

      for (occ2 = occ1 + 1; occ2 < numberOfWrappedFunctions; occ2++)
      {
        sig2 = wrappedFunctions[occ2].Archetype;
        if (sig2 == NULL)
        {
          continue;
        }

        nargs2 = vtkWrap_CountWrappedParameters(sig2);
        vote1 = 0;
        vote2 = 0;

        if (nargs2 == nargs1 && strcmp(sig2->Name, name) == 0)
        {
          allmatch = 1;
          for (i = 0; i < nargs1; i++)
          {
            argmatch = 0;
            val1 = sig1->Parameters[i];
            val2 = sig2->Parameters[i];
            dim1 = (val1->NumberOfDimensions > 0
                ? val1->NumberOfDimensions
                : (vtkWrap_IsPODPointer(val1) || vtkWrap_IsArray(val1)));
            dim2 = (val2->NumberOfDimensions > 0
                ? val2->NumberOfDimensions
                : (vtkWrap_IsPODPointer(val2) || vtkWrap_IsArray(val2)));
            if (dim1 != dim2)
            {
              vote1 = 0;
              vote2 = 0;
              allmatch = 0;
              break;
            }
            else
            {
              baseType1 = (val1->Type & VTK_PARSE_BASE_TYPE);
              baseType2 = (val2->Type & VTK_PARSE_BASE_TYPE);

              unsigned1 = (baseType1 & VTK_PARSE_UNSIGNED);
              unsigned2 = (baseType2 & VTK_PARSE_UNSIGNED);

              baseType1 = (baseType1 & ~VTK_PARSE_UNSIGNED);
              baseType2 = (baseType2 & ~VTK_PARSE_UNSIGNED);

              indirect1 = (val1->Type & VTK_PARSE_INDIRECT);
              indirect2 = (val2->Type & VTK_PARSE_INDIRECT);

              if ((indirect1 == indirect2) && (unsigned1 == unsigned2) &&
                (baseType1 == baseType2) &&
                ((val1->Type & VTK_PARSE_CONST) == (val2->Type & VTK_PARSE_CONST)))
              {
                argmatch = 1;
              }
              /* double precedes float */
              else if ((indirect1 == indirect2) && (baseType1 == VTK_PARSE_DOUBLE) &&
                (baseType2 == VTK_PARSE_FLOAT))
              {
                if (!vote2)
                {
                  vote1 = 1;
                }
              }
              else if ((indirect1 == indirect2) && (baseType1 == VTK_PARSE_FLOAT) &&
                (baseType2 == VTK_PARSE_DOUBLE))
              {
                if (!vote1)
                {
                  vote2 = 1;
                }
              }
              /* unsigned char precedes signed char */
              else if ((indirect1 == indirect2) && ((baseType1 == VTK_PARSE_CHAR) && unsigned1) &&
                (baseType2 == VTK_PARSE_SIGNED_CHAR))
              {
                if (!vote2)
                {
                  vote1 = 1;
                }
              }
              else if ((indirect1 == indirect2) && (baseType1 == VTK_PARSE_SIGNED_CHAR) &&
                ((baseType2 == VTK_PARSE_CHAR) && unsigned2))
              {
                if (!vote1)
                {
                  vote2 = 1;
                }
              }
              /* signed precedes unsigned for everything but char */
              else if ((indirect1 == indirect2) && (baseType1 != VTK_PARSE_CHAR) &&
                (baseType2 != VTK_PARSE_CHAR) && (baseType1 == baseType2) &&
                (unsigned1 != unsigned2))
              {
                if (unsigned2 && !vote2)
                {
                  vote1 = 1;
                }
                else if (unsigned1 && !vote1)
                {
                  vote2 = 1;
                }
              }
              /* integer promotion precedence */
              else if ((indirect1 == indirect2) && (baseType1 == VTK_PARSE_INT) &&
                ((baseType2 == VTK_PARSE_SHORT) || (baseType2 == VTK_PARSE_SIGNED_CHAR) ||
                  ((baseType2 == VTK_PARSE_CHAR) && unsigned2)))
              {
                if (!vote2)
                {
                  vote1 = 1;
                }
              }
              else if ((indirect1 == indirect2) && (baseType2 == VTK_PARSE_INT) &&
                ((baseType1 == VTK_PARSE_SHORT) || (baseType1 == VTK_PARSE_SIGNED_CHAR) ||
                  ((baseType1 == VTK_PARSE_CHAR) && unsigned1)))
              {
                if (!vote1)
                {
                  vote2 = 1;
                }
              }
              /* a string method precedes a "char *" method */
              else if ((baseType2 == VTK_PARSE_CHAR) && (indirect2 == VTK_PARSE_POINTER) &&
                (baseType1 == VTK_PARSE_STRING) &&
                ((indirect1 == VTK_PARSE_REF) || (indirect1 == 0)))
              {
                if (!vote2)
                {
                  vote1 = 1;
                }
              }
              else if ((baseType1 == VTK_PARSE_CHAR) && (indirect1 == VTK_PARSE_POINTER) &&
                (baseType2 == VTK_PARSE_STRING) &&
                ((indirect2 == VTK_PARSE_REF) || (indirect2 == 0)))
              {
                if (!vote1)
                {
                  vote2 = 1;
                }
              }
              /* mismatch: both methods are allowed to live */
              else if ((baseType1 != baseType2) || (unsigned1 != unsigned2) ||
                (indirect1 != indirect2))
              {
                vote1 = 0;
                vote2 = 0;
                allmatch = 0;
                break;
              }
            }

            if (argmatch == 0)
            {
              allmatch = 0;
            }
          }

          /* if all args match, prefer the non-const method */
          if (allmatch)
          {
            if (sig1->IsConst)
            {
              vote2 = 1;
            }
            else if (sig2->IsConst)
            {
              vote1 = 1;
            }
          }
        }

        if (vote1)
        {
          wrappedFunctions[occ2].Archetype = NULL;
        }
        else if (vote2)
        {
          wrappedFunctions[occ1].Archetype = NULL;
          break;
        }

      } /* for (occ2 ... */
    }   /* if (sig1->Name ... */
  }     /* for (occ1 ... */
}

/* -------------------------------------------------------------------- */
/* Print out all the python methods that call the C++ class methods.
 * After they're all printed, a Py_MethodDef array that has function
 * pointers and documentation for each method is printed.  In other
 * words, this poorly named function is "the big one". */

const char* vtkWrapPython_GenerateMethods(FILE* fp, const char* classname, ClassInfo* data,
  FileInfo* finfo, const HierarchyInfo* hinfo, int is_vtkobject, int do_constructors)
{
  int i;
  int fnum;
  int numberOfWrappedFunctions = 0;
  WrappedFunction* wrappedFunctions;
  FunctionInfo* theFunc;
  const char* ccp;
  const char* constructorSignature = NULL;

  /* for tracking functions that will be wrapped */
  wrappedFunctions = (WrappedFunction*)malloc(data->NumberOfFunctions * sizeof(WrappedFunction));

  /* output any custom methods */
  vtkWrapPython_CustomMethods(fp, classname, data, do_constructors);

  /* modify the arg count for vtkDataArray methods */
  vtkWrap_FindCountHints(data, finfo, hinfo);

  /* identify methods that create new instances of objects */
  vtkWrap_FindNewInstanceMethods(data, hinfo);

  /* identify methods that should support __fspath__ protocol */
  vtkWrap_FindFilePathMethods(data);

  /* go through all functions and see which are wrappable */
  for (i = 0; i < data->NumberOfFunctions; i++)
  {
    theFunc = data->Functions[i];

    /* check for wrappability */
    if (vtkWrapPython_MethodCheck(data, theFunc, hinfo) && !theFunc->IsOperator &&
      !theFunc->Template && !vtkWrap_IsDestructor(data, theFunc) &&
      (!vtkWrap_IsConstructor(data, theFunc) == !do_constructors))
    {
      WrappedFunction* wfunc = &wrappedFunctions[numberOfWrappedFunctions++];
      wfunc->Archetype = theFunc;
      ccp = vtkWrapText_PythonSignature(theFunc);
      wfunc->Signature = vtkParse_CacheString(finfo->Strings, ccp, strlen(ccp));
    }
  }

  /* write out the wrapper for each function in the array */
  for (fnum = 0; fnum < numberOfWrappedFunctions; fnum++)
  {
    /* check for type precedence, don't need a "float" method if a
       "double" method exists */
    vtkWrapPython_RemovePrecededMethods(wrappedFunctions, numberOfWrappedFunctions, fnum);

    /* if theFunc wasn't removed, process all its signatures */
    theFunc = wrappedFunctions[fnum].Archetype;
    if (theFunc)
    {
      fprintf(fp, "\n");

      vtkWrapPython_GenerateOneMethod(fp, classname, data, finfo, hinfo, wrappedFunctions,
        numberOfWrappedFunctions, fnum, is_vtkobject, do_constructors);

      if (do_constructors)
      {
        /* return the signature that GenerateOneMethod produced */
        constructorSignature = wrappedFunctions[fnum].Signature;
        break;
      }
    } /* is this method non NULL */
  }   /* loop over all methods */

  /* the method table for constructors is produced elsewhere */
  if (!do_constructors)
  {
    vtkWrapPython_ClassMethodDef(
      fp, classname, data, wrappedFunctions, numberOfWrappedFunctions, fnum);
  }

  free(wrappedFunctions);

  return constructorSignature;
}

/* -------------------------------------------------------------------- */
/* output the MethodDef table for this class */
static void vtkWrapPython_ClassMethodDef(FILE* fp, const char* classname, const ClassInfo* data,
  WrappedFunction* wrappedFunctions, int numberOfWrappedFunctions, int fnum)
{
  int usesMethodKeywords = 0;

  if (strcmp("vtkAlgorithm", data->Name) == 0)
  {
    usesMethodKeywords = 1;
  }

  if (usesMethodKeywords)
  {
    fprintf(fp,
      "/* Ignore the PyCFunction cast warning caused by keyword methods,\n"
      " * Python will know their true type due to the `METH_KEYWORDS` flag. */\n"
      "#if defined(__clang__) && defined(__has_warning)\n"
      "#if __has_warning(\"-Wcast-function-type\")\n"
      "#pragma clang diagnostic push\n"
      "#pragma clang diagnostic ignored \"-Wcast-function-type\"\n"
      "#endif\n"
      "#elif defined(__GNUC__)\n"
      "#pragma GCC diagnostic push\n"
      "#pragma GCC diagnostic ignored \"-Wcast-function-type\"\n"
      "#endif\n\n");
  }

  /* output the method table, with pointers to each function defined above */
  fprintf(fp, "static PyMethodDef Py%s_Methods[] = {\n", classname);

  for (fnum = 0; fnum < numberOfWrappedFunctions; fnum++)
  {
    const FunctionInfo* theFunc = wrappedFunctions[fnum].Archetype;
    if (theFunc)
    {
      /* string literals must be under 2048 chars */
      size_t maxlen = 2040;
      const char* comment;
      const char* signatures;

      /* format the comment nicely to a 66 char width */
      signatures = vtkWrapText_FormatSignature(wrappedFunctions[fnum].Signature, 66, maxlen - 32);
      comment = vtkWrapText_FormatComment(theFunc->Comment, 66);
      comment = vtkWrapText_QuoteString(comment, maxlen - strlen(signatures));

      fprintf(fp, "  {\"%s\", Py%s_%s, METH_VARARGS,\n", theFunc->Name, classname, theFunc->Name);

      fprintf(fp, "   \"%s\\n\\n%s\"},\n", signatures, comment);
    }
  }

  /* vtkObject needs a special entry for AddObserver */
  if (strcmp("vtkObject", data->Name) == 0)
  {
    fprintf(fp,
      "  {\"AddObserver\",  Py%s_AddObserver, 1,\n"
      "   \"AddObserver(self, event:int, command:Callback, priority:float=0.0) -> int\\n"
      "C++: unsigned long AddObserver(const char* event,\\n"
      "    vtkCommand* command, float priority=0.0f)\\n\\n"
      "Add an event callback command(o:vtkObject, event:int) for an event type.\\n"
      "Returns a handle that can be used with RemoveEvent(event:int).\"},\n",
      classname);

    /* vtkObject needs a special entry for InvokeEvent */
    fprintf(fp,
      "{\"InvokeEvent\", PyvtkObject_InvokeEvent, METH_VARARGS,\n"
      "   \"InvokeEvent(self, event:int, callData:Any) -> int\\n"
      "C++: int InvokeEvent(unsigned long event, void* callData)\\n"
      "InvokeEvent(self, event:str, callData:Any) -> int\\n"
      "C++: int InvokeEvent(const char* event, void* callData)\\n"
      "InvokeEvent(self, event:int) -> int\\n"
      "C++: int InvokeEvent(unsigned long event)\\n"
      "InvokeEvent(self, event:str) -> int\\n"
      "C++: int InvokeEvent(const char* event)\\n\\n"
      "This method invokes an event and returns whether the event was\\n"
      "aborted or not. If the event was aborted, the return value is 1,\\n"
      "otherwise it is 0.\"\n},\n");
  }

  /* vtkObjectBase needs GetAddressAsString, UnRegister */
  else if (strcmp("vtkObjectBase", data->Name) == 0)
  {
    fprintf(fp,
      "  {\"GetAddressAsString\",  Py%s_GetAddressAsString, 1,\n"
      "   \"GetAddressAsString(self, classname:str) -> str\\n\\n"
      "Get address of C++ object in format 'Addr=%%p' after casting to\\n"
      "the specified type.  This method is obsolete, you can get the\\n"
      "same information from o.__this__.\"},\n",
      classname);
    fprintf(fp,
      "  {\"Register\", Py%s_Register, 1,\n"
      "   \"Register(self, o:vtkObjectBase)\\nC++: virtual void Register(vtkObjectBase "
      "*o)\\n\\nIncrease the reference count by 1.\\n\"},\n"
      "  {\"UnRegister\", Py%s_UnRegister, 1,\n"
      "   \"UnRegister(self, o:vtkObjectBase)\\n"
      "C++: virtual void UnRegister(vtkObjectBase* o)\\n\\n"
      "Decrease the reference count (release by another object). This\\n"
      "has the same effect as invoking Delete() (i.e., it reduces the\\n"
      "reference count by 1).\\n\"},\n",
      classname, classname);
  }

  /* Adds a new 'update' method on vtkAlgorithm */
  else if (strcmp("vtkAlgorithm", data->Name) == 0)
  {
    fprintf(fp,
      "  {\n"
      "  \"update\", (PyCFunction)PyvtkAlgorithm_update, METH_VARARGS|METH_KEYWORDS,\n"
      "  \"This method updates the pipeline connected to this algorithm\\n\"\n"
      "  \"and returns an Output object with an output property. This property\\n\"\n"
      "  \"provides either a single data object (for algorithms with single output\\n\"\n"
      "  \"or a tuple (for algorithms with multiple outputs).\\n\"\n"
      "  },\n");
  }

  /* python expects the method table to end with a "nullptr" entry */
  fprintf(fp,
    "  {nullptr, nullptr, 0, nullptr}\n"
    "};\n"
    "\n");

  if (usesMethodKeywords)
  {
    fprintf(fp,
      "#if defined(__clang__) && defined(__has_warning)\n"
      "#if __has_warning(\"-Wcast-function-type\")\n"
      "#pragma clang diagnostic pop\n"
      "#endif\n"
      "#elif defined(__GNUC__)\n"
      "#pragma GCC diagnostic pop\n"
      "#endif\n\n");
  }
}

/* -------------------------------------------------------------------- */
/* Check an arg to see if it is wrappable */

static int vtkWrapPython_IsValueWrappable(
  const ClassInfo* data, const ValueInfo* val, const HierarchyInfo* hinfo, int flags)
{
  static const unsigned int wrappableTypes[] = { VTK_PARSE_VOID, VTK_PARSE_BOOL, VTK_PARSE_FLOAT,
    VTK_PARSE_DOUBLE, VTK_PARSE_CHAR, VTK_PARSE_UNSIGNED_CHAR, VTK_PARSE_SIGNED_CHAR, VTK_PARSE_INT,
    VTK_PARSE_UNSIGNED_INT, VTK_PARSE_SHORT, VTK_PARSE_UNSIGNED_SHORT, VTK_PARSE_LONG,
    VTK_PARSE_UNSIGNED_LONG, VTK_PARSE_SSIZE_T, VTK_PARSE_SIZE_T, VTK_PARSE_UNKNOWN,
    VTK_PARSE_LONG_LONG, VTK_PARSE_UNSIGNED_LONG_LONG, VTK_PARSE_OBJECT, VTK_PARSE_QOBJECT,
    VTK_PARSE_STRING, 0 };

  const char* aClass;
  unsigned int baseType;
  int j;

  if ((flags & VTK_WRAP_RETURN) != 0)
  {
    if (vtkWrap_IsVoid(val))
    {
      return 1;
    }

    if (vtkWrap_IsNArray(val))
    {
      return 0;
    }
  }

  /* wrap std::vector<T> (IsScalar means "not pointer or array") */
  if (vtkWrap_IsStdVector(val) && vtkWrap_IsScalar(val))
  {
    int wrappable = 0;
    char* arg = vtkWrap_TemplateArg(val->Class);
    size_t n;
    size_t l = vtkParse_BasicTypeFromString(arg, &baseType, &aClass, &n);

    /* check that type has no following '*' or '[]' decorators */
    if (arg[l] == '\0')
    {
      if (baseType != VTK_PARSE_UNKNOWN && baseType != VTK_PARSE_OBJECT &&
        baseType != VTK_PARSE_QOBJECT && baseType != VTK_PARSE_CHAR)
      {
        for (j = 0; wrappableTypes[j] != 0; j++)
        {
          if (baseType == wrappableTypes[j])
          {
            wrappable = 1;
            break;
          }
        }
      }
      else if (strncmp(arg, "vtkSmartPointer<", 16) == 0)
      {
        if (arg[strlen(arg) - 1] == '>')
        {
          wrappable = 1;
        }
      }
    }

    free(arg);
    return wrappable;
  }

  aClass = val->Class;
  baseType = (val->Type & VTK_PARSE_BASE_TYPE);

  /* go through all types that are indicated as wrappable */
  for (j = 0; wrappableTypes[j] != 0; j++)
  {
    if (baseType == wrappableTypes[j])
    {
      break;
    }
  }
  if (wrappableTypes[j] == 0)
  {
    return 0;
  }

  if (vtkWrap_IsRef(val) && !vtkWrap_IsScalar(val) && !vtkWrap_IsArray(val) &&
    !vtkWrap_IsPODPointer(val))
  {
    return 0;
  }

  if (vtkWrap_IsScalar(val))
  {
    if (vtkWrap_IsNumeric(val) || vtkWrap_IsEnumMember(data, val) || vtkWrap_IsString(val))
    {
      return 1;
    }
    /* enum types were marked in vtkWrapPython_MarkAllEnums() */
    if (val->IsEnum)
    {
      return 1;
    }
    if (vtkWrap_IsVTKSmartPointer(val))
    {
      return 1;
    }
    else if (vtkWrap_IsObject(val) && vtkWrap_IsClassWrapped(hinfo, aClass))
    {
      return 1;
    }
  }
  else if (vtkWrap_IsArray(val) || vtkWrap_IsNArray(val))
  {
    if (vtkWrap_IsNumeric(val))
    {
      return 1;
    }
  }
  else if (vtkWrap_IsPointer(val))
  {
    if (vtkWrap_IsCharPointer(val) || vtkWrap_IsVoidPointer(val) ||
      vtkWrap_IsZeroCopyPointer(val) || vtkWrap_IsPODPointer(val))
    {
      return 1;
    }
    if (vtkWrap_IsPythonObject(val))
    {
      return 1;
    }
    else if (vtkWrap_IsObject(val))
    {
      if (vtkWrap_IsVTKObjectBaseType(hinfo, aClass))
      {
        return 1;
      }
    }
  }

  return 0;
}

/* -------------------------------------------------------------------- */
/* Check a method to see if it is wrappable in python */

int vtkWrapPython_MethodCheck(
  const ClassInfo* data, const FunctionInfo* currentFunction, const HierarchyInfo* hinfo)
{
  int i, n;

  /* some functions will not get wrapped no matter what */
  if (currentFunction->IsExcluded || currentFunction->IsDeleted ||
    currentFunction->Access != VTK_ACCESS_PUBLIC ||
    vtkWrap_IsInheritedMethod(data, currentFunction))
  {
    return 0;
  }

  /* new and delete are meaningless in wrapped languages */
  if (currentFunction->Name == 0 || strcmp("Register", currentFunction->Name) == 0 ||
    strcmp("UnRegister", currentFunction->Name) == 0 ||
    strcmp("Delete", currentFunction->Name) == 0 || strcmp("New", currentFunction->Name) == 0)
  {
    return 0;
  }

  /* function pointer arguments for callbacks */
  if (currentFunction->NumberOfParameters == 2 &&
    vtkWrap_IsVoidFunction(currentFunction->Parameters[0]) &&
    vtkWrap_IsVoidPointer(currentFunction->Parameters[1]) &&
    !vtkWrap_IsConst(currentFunction->Parameters[1]) &&
    vtkWrap_IsVoid(currentFunction->ReturnValue))
  {
    return 1;
  }

  n = vtkWrap_CountWrappedParameters(currentFunction);

  /* check to see if we can handle all the args */
  for (i = 0; i < n; i++)
  {
    if (!vtkWrapPython_IsValueWrappable(data, currentFunction->Parameters[i], hinfo, VTK_WRAP_ARG))
    {
      return 0;
    }
  }

  /* check the return value */
  if (!vtkWrapPython_IsValueWrappable(data, currentFunction->ReturnValue, hinfo, VTK_WRAP_RETURN))
  {
    return 0;
  }

  return 1;
}

/* -------------------------------------------------------------------- */
/* generate code for custom methods for some classes */
/* classname is the Pythonic name, for if data->Name is a templated id */
/* if do_constructors != 0, do constructors, else do any other methods */
static void vtkWrapPython_CustomMethods(
  FILE* fp, const char* classname, ClassInfo* data, int do_constructors)
{
  if (do_constructors == 0)
  {
    /* Modify AddObserver to accept a Python observer methods */
    vtkWrapPython_ReplaceAddObserver(fp, classname, data);

    /* Modify InvokeEvent to allow Python objects as CallData */
    vtkWrapPython_ReplaceInvokeEvent(fp, classname, data);

    /* Make reference counting methods safe, add GetAddressAsString() */
    vtkWrapPython_ObjectBaseMethods(fp, classname, data);

    /* Make collection iterator into a Python iterator */
    vtkWrapPython_CollectionMethods(fp, classname, data);

    /* Add Python-specific update() to algorithm classes */
    vtkWrapPython_AlgorithmMethods(fp, classname, data);
  }
}

/* -------------------------------------------------------------------- */
/* generate pythonic AddObserver method for vtkObject */
static void vtkWrapPython_ReplaceAddObserver(FILE* fp, const char* classname, ClassInfo* data)
{
  int i;
  const FunctionInfo* theFunc;

  /* the python vtkObject needs special hooks for observers */
  if (strcmp("vtkObject", classname) == 0)
  {
    /* Remove the original AddObserver method */
    for (i = 0; i < data->NumberOfFunctions; i++)
    {
      theFunc = data->Functions[i];

      if (strcmp(theFunc->Name, "AddObserver") == 0)
      {
        data->Functions[i]->Name = NULL;
      }
    }

    /* Add the AddObserver method to vtkObject. */
    fprintf(fp,
      "static PyObject *\n"
      "Py%s_AddObserver(PyObject *self, PyObject *args)\n"
      "{\n"
      "  vtkPythonArgs ap(self, args, \"AddObserver\");\n"
      "  vtkObjectBase *vp = ap.GetSelfPointer(self, args);\n"
      "  %s *op = static_cast<%s *>(vp);\n"
      "\n"
      "  const char *temp0s = nullptr;\n"
      "  int temp0i = 0;\n"
      "  PyObject *temp1 = nullptr;\n"
      "  float temp2 = 0.0f;\n"
      "  unsigned long tempr;\n"
      "  PyObject *result = nullptr;\n"
      "  int argtype = 0;\n"
      "\n",
      classname, data->Name, data->Name);

    fprintf(fp,
      "  if (op)\n"
      "  {\n"
      "    if (ap.CheckArgCount(2,3) &&\n"
      "        ap.GetValue(temp0i) &&\n"
      "        ap.GetFunction(temp1) &&\n"
      "        (ap.NoArgsLeft() || ap.GetValue(temp2)))\n"
      "    {\n"
      "      argtype = 1;\n"
      "    }\n"
      "  }\n"
      "\n"
      "  if (op && !argtype)\n"
      "  {\n"
      "    PyErr_Clear();\n"
      "    ap.Reset();\n"
      "\n"
      "    if (ap.CheckArgCount(2,3) &&\n"
      "        ap.GetValue(temp0s) &&\n"
      "        ap.GetFunction(temp1) &&\n"
      "        (ap.NoArgsLeft() || ap.GetValue(temp2)))\n"
      "    {\n"
      "      argtype = 2;\n"
      "    }\n"
      "  }\n"
      "\n");

    fprintf(fp,
      "  if (argtype)\n"
      "  {\n"
      "    vtkPythonCommand *cbc = vtkPythonCommand::New();\n"
      "    cbc->SetObject(temp1);\n"
      "    cbc->SetThreadState(PyThreadState_Get());\n"
      "\n"
      "    if (argtype == 1)\n"
      "    {\n"
      "      if (ap.IsBound())\n"
      "      {\n"
      "        tempr = op->AddObserver(temp0i, cbc, temp2);\n"
      "      }\n"
      "      else\n"
      "      {\n"
      "        tempr = op->%s::AddObserver(temp0i, cbc, temp2);\n"
      "      }\n"
      "    }\n"
      "    else\n"
      "    {\n"
      "      if (ap.IsBound())\n"
      "      {\n"
      "        tempr = op->AddObserver(temp0s, cbc, temp2);\n"
      "      }\n"
      "      else\n"
      "      {\n"
      "        tempr = op->%s::AddObserver(temp0s, cbc, temp2);\n"
      "      }\n"
      "    }\n"
      "    PyVTKObject_AddObserver(self, tempr);\n"
      "\n",
      data->Name, data->Name);

    fprintf(fp,
      "    cbc->Delete();\n"
      "\n"
      "    if (!ap.ErrorOccurred())\n"
      "    {\n"
      "      result = ap.BuildValue(tempr);\n"
      "    }\n"
      "  }\n"
      "\n"
      "  return result;\n"
      "}\n"
      "\n");
  }
}

/* -------------------------------------------------------------------- */
/* generate data handlers for InvokeEvent method for vtkObject */
static void vtkWrapPython_ReplaceInvokeEvent(FILE* fp, const char* classname, ClassInfo* data)
{
  int i;
  const FunctionInfo* theFunc;

  /* the python vtkObject needs a special InvokeEvent to turn any
     calldata into an appropriately unwrapped void pointer */

  /* different types of callback data */

  int numCallBackTypes = 5;

  static const char* callBackTypeString[] = { "z", "", "i", "d", "V" };

  static const char* fullCallBackTypeString[] = { "z", "", "i", "d", "V *vtkObjectBase" };

  static const char* callBackTypeDecl[] = { "  const char *calldata = nullptr;\n", "",
    "  long calldata;\n", "  double calldata;\n", "  vtkObjectBase *calldata = nullptr;\n" };

  static const char* callBackReadArg[] = { " &&\n      ap.GetValue(calldata)", "",
    " &&\n      ap.GetValue(calldata)", " &&\n      ap.GetValue(calldata)",
    " &&\n      ap.GetVTKObject(calldata, \"vtkObject\")" };

  static const char* methodCallSecondHalf[] = { ", const_cast<char *>(calldata)", "", ", &calldata",
    ", &calldata", ", calldata" };

  /* two ways to refer to an event */
  static const char* eventTypeString[] = { "L", "z" };
  static const char* eventTypeDecl[] = { "  unsigned long event;\n",
    "  const char *event = nullptr;\n" };

  int callBackIdx, eventIdx;

  /* the python vtkObject needs special hooks for observers */
  if (strcmp("vtkObject", classname) == 0)
  {
    /* Remove the original InvokeEvent method */
    for (i = 0; i < data->NumberOfFunctions; i++)
    {
      theFunc = data->Functions[i];

      if (theFunc->Name && strcmp(theFunc->Name, "InvokeEvent") == 0)
      {
        data->Functions[i]->Name = NULL;
      }
    }

    /* Add the InvokeEvent method to vtkObject. */
    fprintf(fp,
      "// This collection of methods that handle InvokeEvent are\n"
      "// generated by a special case in vtkWrapPythonMethodDef.c\n"
      "// The last characters of the method name indicate the type signature\n"
      "// of the overload they handle: for example, \"_zd\" indicates that\n"
      "// the event type is specified by string and the calldata is a double\n");

    for (callBackIdx = 0; callBackIdx < numCallBackTypes; callBackIdx++)
    {
      for (eventIdx = 0; eventIdx < 2; eventIdx++)
      {
        fprintf(fp,
          "static PyObject *\n"
          "PyvtkObject_InvokeEvent_%s%s(PyObject *self, PyObject *args)\n"
          "{\n"
          "  vtkPythonArgs ap(self, args, \"InvokeEvent\");\n"
          "  vtkObjectBase *vp = ap.GetSelfPointer(self, args);\n"
          "  vtkObject *op = static_cast<vtkObject *>(vp);\n"
          "\n"
          "%s%s"
          "  PyObject *result = nullptr;\n"
          "\n"
          "  if (op && ap.CheckArgCount(%d) &&\n"
          "      ap.GetValue(event)%s)\n"
          "  {\n"
          "    int tempr = op->InvokeEvent(event%s);\n"
          "\n"
          "    if (!ap.ErrorOccurred())\n"
          "    {\n"
          "      result = ap.BuildValue(tempr);\n"
          "    }\n"
          "  }\n"
          "  return result;\n"
          "}\n"
          "\n",
          eventTypeString[eventIdx], callBackTypeString[callBackIdx], eventTypeDecl[eventIdx],
          callBackTypeDecl[callBackIdx], 1 + (callBackReadArg[callBackIdx][0] != '\0'),
          callBackReadArg[callBackIdx], methodCallSecondHalf[callBackIdx]);
      }
    }
    fprintf(fp, "static PyMethodDef PyvtkObject_InvokeEvent_Methods[] = {\n");
    for (callBackIdx = 0; callBackIdx < numCallBackTypes; callBackIdx++)
    {
      for (eventIdx = 0; eventIdx < 2; eventIdx++)
      {
        fprintf(fp,
          "  {\"InvokeEvent\", PyvtkObject_InvokeEvent_%s%s, METH_VARARGS,\n"
          "   \"@%s%s\"},\n",
          eventTypeString[eventIdx], callBackTypeString[callBackIdx], eventTypeString[eventIdx],
          fullCallBackTypeString[callBackIdx]);
      }
    }

    fprintf(fp,
      "  {nullptr, nullptr, 0, nullptr}\n"
      "};\n"
      "\n"
      "static PyObject *\n"
      "PyvtkObject_InvokeEvent(PyObject *self, PyObject *args)\n"
      "{\n"
      "  PyMethodDef *methods = PyvtkObject_InvokeEvent_Methods;\n"
      "  int nargs = vtkPythonArgs::GetArgCount(self, args);\n"
      "\n"
      "  switch(nargs)\n"
      "  {\n"
      "    case 1:\n"
      "    case 2:\n"
      "      return vtkPythonOverload::CallMethod(methods, self, args);\n"
      "  }\n"
      "\n"
      "  vtkPythonArgs::ArgCountError(nargs, \"InvokeEvent\");\n"
      "  return nullptr;\n"
      "}\n");
  }
}

/* -------------------------------------------------------------------- */
/* generate custom methods needed for vtkObjectBase */
static void vtkWrapPython_ObjectBaseMethods(FILE* fp, const char* classname, ClassInfo* data)
{
  int i;
  FunctionInfo* theFunc;

  /* the python vtkObjectBase needs a couple extra functions */
  if (strcmp("vtkObjectBase", classname) == 0)
  {
    /* remove the original methods, if they exist */
    for (i = 0; i < data->NumberOfFunctions; i++)
    {
      theFunc = data->Functions[i];

      if ((strcmp(theFunc->Name, "GetAddressAsString") == 0) ||
        (strcmp(theFunc->Name, "Register") == 0) || (strcmp(theFunc->Name, "UnRegister") == 0))
      {
        theFunc->Name = NULL;
      }
    }

    /* add the GetAddressAsString method to vtkObjectBase */
    fprintf(fp,
      "static PyObject *\n"
      "Py%s_GetAddressAsString(PyObject *self, PyObject *args)\n"
      "{\n"
      "  vtkPythonArgs ap(self, args, \"GetAddressAsString\");\n"
      "  vtkObjectBase *op = ap.GetSelfPointer(self, args);\n"
      "\n"
      "  const char *temp0;\n"
      "  char tempr[256];\n"
      "  PyObject *result = nullptr;\n"
      "\n"
      "  if (op && ap.CheckArgCount(1) &&\n"
      "      ap.GetValue(temp0))\n"
      "  {\n"
      "    snprintf(tempr, sizeof(tempr), \"Addr=%%p\", static_cast<void*>(op));\n"
      "\n"
      "    result = ap.BuildValue(tempr);\n"
      "  }\n"
      "\n"
      "  return result;\n"
      "}\n"
      "\n",
      classname);

    /* Override the Register method to check whether to ignore Register */
    fprintf(fp,
      "static PyObject *\n"
      "Py%s_Register(PyObject *self, PyObject *args)\n"
      "{\n"
      "  vtkPythonArgs ap(self, args, \"Register\");\n"
      "  vtkObjectBase *op = ap.GetSelfPointer(self, args);\n"
      "\n"
      "  vtkObjectBase *temp0 = nullptr;\n"
      "  PyObject *result = nullptr;\n"
      "\n"
      "  if (op && ap.CheckArgCount(1) &&\n"
      "      ap.GetVTKObject(temp0, \"vtkObjectBase\"))\n"
      "  {\n"
      "    if (!PyVTKObject_Check(self) ||\n"
      "        (PyVTKObject_GetFlags(self) & VTK_PYTHON_IGNORE_UNREGISTER) == 0)\n"
      "    {\n"
      "      if (ap.IsBound())\n"
      "      {\n"
      "        op->Register(temp0);\n"
      "      }\n"
      "      else\n"
      "      {\n"
      "        op->vtkObjectBase::Register(temp0);\n"
      "      }\n"
      "    }\n"
      "\n"
      "    if (!ap.ErrorOccurred())\n"
      "    {\n"
      "      result = ap.BuildNone();\n"
      "    }\n"
      "  }\n"
      "\n"
      "  return result;\n"
      "}\n"
      "\n",
      classname);

    /* Override the UnRegister method to check whether to ignore UnRegister */
    fprintf(fp,
      "static PyObject *\n"
      "Py%s_UnRegister(PyObject *self, PyObject *args)\n"
      "{\n"
      "  vtkPythonArgs ap(self, args, \"UnRegister\");\n"
      "  vtkObjectBase *op = ap.GetSelfPointer(self, args);\n"
      "\n"
      "  vtkObjectBase *temp0 = nullptr;\n"
      "  PyObject *result = nullptr;\n"
      "\n"
      "  if (op && ap.CheckArgCount(1) &&\n"
      "      ap.GetVTKObject(temp0, \"vtkObjectBase\"))\n"
      "  {\n"
      "    if (!PyVTKObject_Check(self) ||\n"
      "        (PyVTKObject_GetFlags(self) & VTK_PYTHON_IGNORE_UNREGISTER) == 0)\n"
      "    {\n"
      "      if (ap.IsBound())\n"
      "      {\n"
      "        op->UnRegister(temp0);\n"
      "      }\n"
      "      else\n"
      "      {\n"
      "        op->vtkObjectBase::UnRegister(temp0);\n"
      "      }\n"
      "    }\n"
      "\n"
      "    if (!ap.ErrorOccurred())\n"
      "    {\n"
      "      result = ap.BuildNone();\n"
      "    }\n"
      "  }\n"
      "\n"
      "  return result;\n"
      "}\n"
      "\n",
      classname);
  }
}

/* -------------------------------------------------------------------- */
/* generate custom methods needed for vtkCollection */
static void vtkWrapPython_CollectionMethods(FILE* fp, const char* classname, const ClassInfo* data)
{
  if (strcmp("vtkCollection", classname) == 0)
  {
    fprintf(fp,
      "static PyObject *\n"
      "PyvtkCollection_Iter(PyObject *self)\n"
      "{\n"
      "  PyVTKObject *vp = (PyVTKObject *)self;\n"
      "  %s *op = static_cast<%s *>(vp->vtk_ptr);\n"
      "\n"
      "  PyObject *result = nullptr;\n"
      "\n"
      "  if (op)\n"
      "  {\n"
      "    vtkCollectionIterator *tempr = op->NewIterator();\n"
      "    if (tempr != nullptr)\n"
      "    {\n"
      "      result = vtkPythonArgs::BuildVTKObject(tempr);\n"
      "      PyVTKObject_GetObject(result)->UnRegister(nullptr);\n"
      "    }\n"
      "  }\n"
      "\n"
      "  return result;\n"
      "}\n",
      data->Name, data->Name);
  }

  if (strcmp("vtkCollectionIterator", classname) == 0)
  {
    fprintf(fp,
      "static PyObject *\n"
      "PyvtkCollectionIterator_Next(PyObject *self)\n"
      "{\n"
      "  PyVTKObject *vp = (PyVTKObject *)self;\n"
      "  %s *op = static_cast<%s*>(vp->vtk_ptr);\n"
      "\n"
      "  PyObject *result = nullptr;\n"
      "\n"
      "  if (op)\n"
      "  {\n"
      "    vtkObject *tempr = op->GetCurrentObject();\n"
      "    op->GoToNextItem();\n"
      "    if (tempr != nullptr)\n"
      "    {\n"
      "      result = vtkPythonArgs::BuildVTKObject(tempr);\n"
      "    }\n"
      "  }\n"
      "\n"
      "  return result;\n"
      "}\n"
      "\n"
      "static PyObject *\n"
      "PyvtkCollectionIterator_Iter(PyObject *self)\n"
      "{\n"
      "  Py_INCREF(self);\n"
      "  return self;\n"
      "}\n",
      data->Name, data->Name);
  }
}

/* -------------------------------------------------------------------- */
/* generate custom methods needed for vtkCollection */
static void vtkWrapPython_AlgorithmMethods(FILE* fp, const char* classname, const ClassInfo* data)
{
  if (strcmp("vtkAlgorithm", classname) == 0)
  {
    fprintf(fp,
      "static PyObject *\n"
      "PyvtkAlgorithm_Call(PyObject* self, PyObject* args, PyObject* /*kwargs*/)\n"
      "{\n"
      "  int nargs = vtkPythonArgs::GetArgCount(self, args);\n"
      "  if (nargs > 1)\n"
      "  {\n"
      "    // Could call vtkPythonArgs::ArgCountError here, but MSVC confuses the\n"
      "    // intended static overload with a non-static overload and raises C4753.\n"
      "    char text[256];\n"
      "    snprintf(text, sizeof(text),\n"
      "      \"no overloads of __call__() take %%d argument%%s\",\n"
      "      nargs, (nargs == 1 ? \"\" : \"s\"));\n"
      "    PyErr_SetString(PyExc_TypeError, text);\n"
      "    return nullptr;\n"
      "  }\n"
      "  vtkPythonArgs ap(self, args, \"__call__\");\n"
      "  vtkObjectBase* vp = ap.GetSelfPointer(self, args);\n"
      "  vtkAlgorithm* op = vtkAlgorithm::SafeDownCast(vp);\n"
      "  if (op == nullptr)\n"
      "  {\n"
      "    PyErr_SetString(PyExc_TypeError,\n"
      "      \"The call operator must be invoked on a vtkAlgorithm\");\n"
      "    return nullptr;\n"
      "  }\n"
      "  vtkDataObject* input = nullptr;\n"
      "  PyObject* output = nullptr;\n"
      "  if (op)\n"
      "  {\n"
      "    if (nargs == 0)\n"
      "    {\n"
      "      if (op->GetNumberOfInputPorts())\n"
      "      {\n"
      "        PyErr_SetString(PyExc_ValueError,\n"
      "          \"No input was provided when one is required.\");\n"
      "        return nullptr;\n"
      "      }\n"
      "    }\n"
      "    int numOutputPorts = op->GetNumberOfOutputPorts();\n"
      "    std::vector<vtkAlgorithmOutput*> inpConns;\n"
      "    std::vector<vtkDataObject*> inputs;\n"
      "    if (nargs == 1 && op->GetNumberOfInputPorts() < 1)\n"
      "    {\n"
      "      PyErr_SetString(PyExc_ValueError,\n"
      "        \"Trying to set input on an algorithm with 0 input ports\");\n"
      "      return nullptr;\n"
      "    }\n"
      "    if (nargs == 1)\n"
      "    {\n"
      "      PyObject* obj = PyTuple_GetItem(args, 0);\n"
      "      if (PySequence_Check(obj))\n"
      "      {\n"
      "         Py_ssize_t nInps = PySequence_Size(obj);\n"
      "         for (Py_ssize_t i=0; i < nInps; i++)\n"
      "         {\n"
      "           PyObject* s = PySequence_GetItem(obj, i);\n"
      "           vtkDataObject* dobj = vtkDataObject::SafeDownCast(\n"
      "               vtkPythonUtil::GetPointerFromObject(s, \"vtkDataObject\"));\n"
      "           if (dobj)\n"
      "           {\n"
      "             inputs.push_back(dobj);\n"
      "           }\n"
      "           else\n"
      "           {\n"
      "             PyErr_SetString(PyExc_ValueError,\n"
      "               \"Expecting a sequence of data objects or a single data object as "
      "input.\");\n"
      "             return nullptr;\n"
      "           }\n"
      "         }\n"
      "      }\n"
      "      else if (ap.GetVTKObject(input, \"vtkDataObject\"))\n"
      "      {\n"
      "        inputs.push_back(input);\n"
      "      }\n"
      "      else\n"
      "      {\n"
      "        PyErr_SetString(PyExc_ValueError,\n"
      "          \"Expecting a sequence of data objects or a single data object as input.\");\n"
      "        return nullptr;\n"
      "      }\n"
      "\n");

    fprintf(fp,
      "      int nConns = op->GetNumberOfInputConnections(0);\n"
      "      for (int i=0; i < nConns; i++)\n"
      "      {\n"
      "        auto conn = op->GetInputConnection(0, i);\n"
      "        inpConns.push_back(conn);\n"
      "        if (conn && conn->GetProducer())\n"
      "        {\n"
      "          conn->GetProducer()->Register(nullptr);\n"
      "        }\n"
      "      }\n"
      "      op->RemoveAllInputConnections(0);\n"
      "      for (vtkDataObject* inputDobj : inputs)\n"
      "      {\n"
      "        vtkTrivialProducer* tp = vtkTrivialProducer::New();\n"
      "        tp->SetOutput(inputDobj);\n"
      "        op->AddInputConnection(0, tp->GetOutputPort());\n"
      "        tp->Delete();\n"
      "      }\n"
      "    }\n"
      "    op->Update();\n"
      "    if (numOutputPorts > 1)\n"
      "    {\n"
      "      output = PyTuple_New(numOutputPorts);\n"
      "      for (int i=0; i < numOutputPorts; i++)\n"
      "      {\n"
      "        auto dobj = op->GetOutputDataObject(i);\n"
      "        auto copy = dobj->NewInstance();\n"
      "        copy->ShallowCopy(dobj);\n"
      "        auto anOutput = ap.BuildVTKObject(copy);\n"
      "        PyTuple_SetItem(output, i, anOutput);\n"
      "        copy->UnRegister(nullptr);\n"
      "      }\n"
      "    }\n"
      "    else if (op->GetNumberOfOutputPorts() == 1)\n"
      "    {\n"
      "      auto dobj = op->GetOutputDataObject(0);\n"
      "      auto copy = dobj->NewInstance();\n"
      "      copy->ShallowCopy(dobj);\n"
      "      output = ap.BuildVTKObject(copy);\n"
      "      copy->UnRegister(nullptr);\n"
      "    }\n"
      "    else\n"
      "    {\n"
      "      output = ap.BuildNone();\n"
      "    }\n"
      "    if (op->GetNumberOfInputPorts())\n"
      "    {\n"
      "      op->RemoveAllInputConnections(0);\n"
      "      for (auto conn : inpConns)\n"
      "      {\n"
      "        op->AddInputConnection(0, conn);\n"
      "        if (conn && conn->GetProducer())\n"
      "        {\n"
      "          conn->GetProducer()->UnRegister(nullptr);\n"
      "        }\n"
      "      }\n"
      "    }\n"
      "  }\n"
      "  return output;\n"
      "}\n\n");

    fprintf(fp,
      "static PyObject *\n"
      "Py%s_update(PyObject* self, PyObject* args, PyObject* kwargs)\n"
      "{\n"
      "  vtkPythonArgs ap(self, args, \"update\");\n"
      "  PyObject *output = nullptr;\n"
      "  if (ap.CheckArgCount(0))\n"
      "  {\n"
      "    PyObject *moduleName = PyUnicode_DecodeFSDefault(\"vtkmodules.util.execution_model\");\n"
      "    PyObject *internalModule = PyImport_Import(moduleName);\n"
      "    Py_DECREF(moduleName);\n"
      "    if (internalModule != nullptr)\n"
      "    {\n"
      "      // Get the class from the module\n"
      "      PyObject *outputClass = PyObject_GetAttrString(internalModule, \"Output\");\n"
      "      if (outputClass != nullptr)\n"
      "      {\n"
      "        // Create an instance of the class\n"
      "        auto* self_arg = PyTuple_Pack(1, self);\n"
      "        output = PyObject_Call(outputClass, self_arg, kwargs);\n"
      "        Py_XDECREF(self_arg);\n"
      "        if (output == nullptr)\n"
      "        {\n"
      "          return nullptr;\n"
      "        }\n"
      "        Py_DECREF(outputClass);\n"
      "      }\n"
      "      else\n"
      "      {\n"
      "         return nullptr;\n"
      "      }\n"
      "      Py_DECREF(internalModule);\n"
      "    }\n"
      "    else\n"
      "    {\n"
      "      return nullptr;\n"
      "    }\n"
      "  }\n"
      "  return output;\n"
      "}\n",
      data->Name);
  }
}
