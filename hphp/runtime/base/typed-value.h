/*
   +----------------------------------------------------------------------+
   | HipHop for PHP                                                       |
   +----------------------------------------------------------------------+
   | Copyright (c) 2010-2016 Facebook, Inc. (http://www.facebook.com)     |
   +----------------------------------------------------------------------+
   | This source file is subject to version 3.01 of the PHP license,      |
   | that is bundled with this package in the file LICENSE, and is        |
   | available through the world-wide-web at the following url:           |
   | http://www.php.net/license/3_01.txt                                  |
   | If you did not receive a copy of the PHP license and are unable to   |
   | obtain it through the world-wide-web, please send a note to          |
   | license@php.net so we can mail you a copy immediately.               |
   +----------------------------------------------------------------------+
*/
#ifndef incl_HPHP_HPHPVALUE_H_
#define incl_HPHP_HPHPVALUE_H_

#include <type_traits>
#include <string>
#include <cstdint>
#include <cstdlib>

#include "hphp/runtime/base/datatype.h"

#include "hphp/util/type-scan.h"

namespace HPHP {

//////////////////////////////////////////////////////////////////////

struct Class;
struct ArrayData;
struct StringData;
struct ObjectData;
struct RefData;
struct ResourceHdr;
struct TypedValue;

//////////////////////////////////////////////////////////////////////

/*
 * This is the payload of a PHP value.  This union may only be used in
 * contexts that have a discriminator, e.g. in TypedValue (below), or
 * when the type is known beforehand.
 */
union Value {
  int64_t       num;    // KindOfInt64, KindOfBool (must be zero-extended)
  double        dbl;    // KindOfDouble
  StringData*   pstr;   // KindOfString, KindOfPersistentString
  ArrayData*    parr;   // KindOfArray, KindOfVec, KindOfDict, KindOfKeyset
  ObjectData*   pobj;   // KindOfObject
  ResourceHdr*  pres;   // KindOfResource
  Class*        pcls;   // only in vm stack, no type tag.
  RefData*      pref;   // KindOfRef
};

enum VarNrFlag { NR_FLAG = 1<<29 };

struct ConstModifiers {
  bool m_isAbstract;
  bool m_isType;
};

union AuxUnion {
  // Undiscriminated raw value.
  uint32_t u_raw;
  // True if we're suspending an FCallAwait.
  uint32_t u_fcallAwaitFlag;
  // Key type and hash for MixedArray.
  int32_t u_hash;
  // Magic number for asserts in VarNR.
  VarNrFlag u_varNrFlag;
  // Used by Class::initPropsImpl() for deep init.
  bool u_deepInit;
  // Used by unit.cpp to squirrel away RDS handles.
  int32_t u_rdsHandle;
  // Used by Class::Const.
  ConstModifiers u_constModifiers;
};

/*
 * 7pack format:
 * experimental "Packed" format for TypedValues.  By grouping 7 tags
 * and 7 values separately, we can fit 7 TypedValues in 63 bytes (64 with
 * a throw-away alignment byte (t0):
 *
 *   0   1   2     7   8       16        56
 *   [t0][t1][t2]..[t7][value1][value2]..[value7]
 *
 * With this layout, a single TypedValue requires 16 bytes, and still has
 * room for a 32-bit padding field, which we still use in a few places:
 *
 *   0   1       2   3   4      8
 *   [t0][m_type][t2][t3][m_pad][m_data]
 */

/*
 * A TypedValue is a descriminated PHP Value.  m_tag describes the contents
 * of m_data.  m_aux is described above, and must only be read or written
 * in specialized contexts.
 */
struct TypedValue {
  Value m_data;
  DataType m_type;
  AuxUnion m_aux;

  std::string pretty() const; // debug formatting. see trace.h

  TYPE_SCAN_CUSTOM() {
    switch (m_type) {
      case KindOfObject: scanner.enqueue(m_data.pobj); break;
      case KindOfResource: scanner.enqueue(m_data.pres); break;
      case KindOfString: scanner.enqueue(m_data.pstr); break;
      case KindOfVec:
      case KindOfDict:
      case KindOfKeyset:
      case KindOfArray: scanner.enqueue(m_data.parr); break;
      case KindOfRef: scanner.enqueue(m_data.pref); break;
      case KindOfUninit:
      case KindOfNull:
      case KindOfBoolean:
      case KindOfInt64:
      case KindOfDouble:
      case KindOfPersistentString:
      case KindOfPersistentArray:
      case KindOfPersistentVec:
      case KindOfPersistentDict:
      case KindOfPersistentKeyset:
      case KindOfClass:
        break;
    }
  }
};

// Check that TypedValue's size is a power of 2 (16bytes currently)
static_assert((sizeof(TypedValue) & (sizeof(TypedValue)-1)) == 0,
              "TypedValue's size is expected to be a power of 2");
constexpr size_t kTypedValueAlignMask = sizeof(TypedValue) - 1;
constexpr size_t alignTypedValue(size_t sz) {
  return (sz + kTypedValueAlignMask) & ~kTypedValueAlignMask;
}

/*
 * This TypedValue subclass exposes a 32-bit "aux" field somewhere inside it.
 */
struct TypedValueAux : TypedValue {
  static constexpr size_t auxOffset = offsetof(TypedValue, m_aux);
  static const size_t auxSize = sizeof(decltype(m_aux));
  int32_t& hash() { return m_aux.u_hash; }
  const int32_t& hash() const { return m_aux.u_hash; }
  int32_t& rdsHandle() { return m_aux.u_rdsHandle; }
  const int32_t& rdsHandle() const { return m_aux.u_rdsHandle; }
  bool& deepInit() { return m_aux.u_deepInit; }
  const bool& deepInit() const { return m_aux.u_deepInit; }
  ConstModifiers& constModifiers() { return m_aux.u_constModifiers; }
  const ConstModifiers& constModifiers() const {
    return m_aux.u_constModifiers;
  }
  VarNrFlag& varNrFlag() { return m_aux.u_varNrFlag; }
  const VarNrFlag& varNrFlag() const { return m_aux.u_varNrFlag; }

private:
  static void assertions() {
    static_assert(sizeof(TypedValueAux) <= 16,
                  "don't add big things to AuxUnion");
  }
};

/*
 * Sometimes TypedValues need to be allocated with alignment that
 * allows use of 128-bit SIMD stores/loads.  This constant just helps
 * self-document that case.
 */
constexpr size_t kTVSimdAlign = 0x10;

/*
 * These may be used to provide a little more self-documentation about
 * whether typed values must be cells (not KindOfRef) or ref (must be
 * KindOfRef).
 *
 * See bytecode.specification for details.  Note that in
 * bytecode.specification, refs are abbreviated as "V".
 *
 */
typedef TypedValue Cell;
typedef TypedValue Ref;

/*
 * A TypedNum is a TypedValue that is either KindOfDouble or
 * KindOfInt64.
 */
typedef TypedValue TypedNum;

//////////////////////////////////////////////////////////////////////

/*
 * Assertions on Cells and TypedValues.  Should usually only happen
 * inside an assert().
 */
bool tvIsPlausible(TypedValue);
bool cellIsPlausible(Cell);
bool refIsPlausible(Ref);

/////////////////////////////////////////////////////////////////////

template<DataType> struct DataTypeCPPType;
#define X(dt, cpp) \
template<> struct DataTypeCPPType<dt> { typedef cpp type; }

X(KindOfUninit,       void);
X(KindOfNull,         void);
X(KindOfBoolean,      bool);
X(KindOfInt64,        int64_t);
X(KindOfDouble,       double);
X(KindOfArray,        ArrayData*);
X(KindOfPersistentArray,  const ArrayData*);
X(KindOfVec,          ArrayData*);
X(KindOfPersistentVec, const ArrayData*);
X(KindOfDict,         ArrayData*);
X(KindOfPersistentDict, const ArrayData*);
X(KindOfKeyset,       ArrayData*);
X(KindOfPersistentKeyset, const ArrayData*);
X(KindOfObject,       ObjectData*);
X(KindOfResource,     ResourceHdr*);
X(KindOfRef,          RefData*);
X(KindOfString,       StringData*);
X(KindOfPersistentString, const StringData*);

#undef X

/*
 * make_value and make_tv are helpers for creating TypedValues and
 * Values as temporaries, without messing up the conversions.
 */

template<class T>
typename std::enable_if<
  std::is_integral<T>::value,
  Value
>::type make_value(T t) { Value v; v.num = t; return v; }

template<class T>
typename std::enable_if<
  std::is_pointer<T>::value,
  Value
>::type make_value(T t) {
  Value v;
  v.num = reinterpret_cast<int64_t>(t);
  return v;
}

inline Value make_value(double d) { Value v; v.dbl = d; return v; }

/**
 * Pack a base data element into a TypedValue for use
 * elsewhere in the runtime.
 *
 * TypedValue tv = make_tv<KindOfInt64>(123);
 */
template<DataType DType>
typename std::enable_if<
  !std::is_same<typename DataTypeCPPType<DType>::type,void>::value,
  TypedValue
>::type make_tv(typename DataTypeCPPType<DType>::type val) {
  TypedValue ret;
  ret.m_data = make_value(val);
  ret.m_type = DType;
  assert(tvIsPlausible(ret));
  return ret;
}

template<DataType DType>
typename std::enable_if<
  std::is_same<typename DataTypeCPPType<DType>::type,void>::value,
  TypedValue
>::type make_tv() {
  TypedValue ret;
  ret.m_type = DType;
  assert(tvIsPlausible(ret));
  return ret;
}

/**
 * Extract the underlying data element for the TypedValue
 *
 * int64_t val = unpack_tv<KindOfInt64>(tv);
 */
template <DataType DType>
typename std::enable_if<
  std::is_same<typename DataTypeCPPType<DType>::type,double>::value,
  double
>::type unpack_tv(TypedValue *tv) {
  assert(DType == tv->m_type);
  assert(tvIsPlausible(*tv));
  return tv->m_data.dbl;
}

template <DataType DType>
typename std::enable_if<
  std::is_integral<typename DataTypeCPPType<DType>::type>::value,
  typename DataTypeCPPType<DType>::type
>::type unpack_tv(TypedValue *tv) {
  assert(DType == tv->m_type);
  assert(tvIsPlausible(*tv));
  return tv->m_data.num;
}

template <DataType DType>
typename std::enable_if<
  std::is_pointer<typename DataTypeCPPType<DType>::type>::value,
  typename DataTypeCPPType<DType>::type
>::type unpack_tv(TypedValue *tv) {
  assert((DType == tv->m_type) ||
         (isStringType(DType) && isStringType(tv->m_type)) ||
         (isArrayType(DType) && isArrayType(tv->m_type)) ||
         (isVecType(DType) && isVecType(tv->m_type)) ||
         (isDictType(DType) && isDictType(tv->m_type)) ||
         (isKeysetType(DType) && isKeysetType(tv->m_type)));
  assert(tvIsPlausible(*tv));
  return reinterpret_cast<typename DataTypeCPPType<DType>::type>
           (tv->m_data.pstr);
}

//////////////////////////////////////////////////////////////////////

}

#endif
