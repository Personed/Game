/*!
\file smart pointers, simple referencing and auto deleting dynamic objects
*/

#ifdef _MSC_VER
#pragma once
#endif


#ifndef _POINTERS_HPP
#define _POINTERS_HPP

#include <Es/Common/win.h>

#include <Es/Containers/typeOpts.hpp>
#include <Es/Framework/debugLog.hpp>
#include <Es/Memory/normalNew.hpp>

#if defined _CPPRTTI
  #include "typeinfo.h"
#endif

//! smart pointers for COM/IUnknown objects.
/*!
  Reference counting automation. This template class provides a reference
  substitution for smart pointer operations.
  \sa Ref, RefCount, SRef
*/
template<class Type>
class ComRef
{
private:
  //! Reference to the COM object.
  Type *_ref;

  /// prevent assigning normal pointers using = , use += or << instead
  void operator = ( Type *source );

public:
  //! Default constructor.
  ComRef(){_ref=NULL;}
  //! Constructor.
  /*!
    Initialization through temporary pointer. Be carefull using this...
    \param ptr Pointer to COM object to assign.
  */
  //ComRef( Type *ptr ){_ref=ptr;}
  //! Copying of pointer. Present pointer will be released.
  /*!
    Note that this method will not call the AddRef() method. It is
    expected it was already called (like with QueryInterface method)
    Using this method you must be VERY careful.
    \param source Pointer to COM object to assign.
  */
  void operator << ( Type *source )
  {
    Free();
    _ref=source;
  }

  // assign a plain pointer, assume normal reference counting behavior
  void operator += ( Type *source )
  {
    if( source ) source->AddRef();
    Free();
    _ref=source;
  }
  //! Determining whether pointer is not null.
  /*!
    \return TRUE in case pointer is not null, FALSE otherwise.
  */
  bool NotNull() const {return _ref!=NULL;}
  //! Determining whether pointer is null.
  /*!
    \return TRUE in case pointer is null, FALSE otherwise.
  */
  bool IsNull() const {return _ref==NULL;}
  //! Copy constructor.
  /*!
    \param source Source pointer to copy.
  */
  ComRef( const ComRef &source ){_ref=source._ref;if( _ref ) _ref->AddRef();}
  //! Copying of pointer. Present pointer will be released.
  /*!
    \param sRef Source pointer to assign.
  */
  void operator = ( const ComRef &sRef )
  {
    Type *source=sRef._ref;
    if( source ) source->AddRef();
    Free();
    _ref=source;
  }
  //! Releasing of pointer to COM object.
  /*!
    \return Resulting value of the reference count.
  */
  int Free(){int ret=0;if( _ref ) ret=_ref->Release(),_ref=NULL;return ret;}
  //! Destructor.
  __forceinline ~ComRef(){Free();}
  //! Retrieving the pointer.
  /*!
    \return Pointer to the COM object.
  */
  __forceinline Type *operator -> () const {return _ref;}
  //! Retrieving the pointer.
  /*!
    \return Pointer to the COM object.
  */
  __forceinline Type *GetRef() const {return _ref;}
  //! Retrieving the pointer.
  /*!
    \return Pointer to the COM object.
  */
  __forceinline operator Type *() const {return _ref;} // casting
  //! Initialization of the COM object.
  /*!
    This method clears present pointer and returns pointer to this pointer
    and expects user will initialize it properly by himself (f.i. it's up to
    him to call the AddRef() method), therefore you must be VERY careful to
    use this method.
    \return Pointer to pointer to new COM object.
  */
  Type **Init() {Free();return &_ref;} // initialization will be performed outside
  //! Initialization of the COM object. 
  /*!
    Same as Init, but returns void **. Some Com functions (such as QueryInterface) 
    needs void ** and will not work with Init()
    @see Init
    */
  void **InitVoid() {return reinterpret_cast<void **>(Init());} 

  //! Retrieving of number of references.
  /*!
    This method is suitable for debugging and testing purposes only.
  */
  int GetRefCount() { _ref->AddRef(); return _ref->Release(); }
  ClassIsMovableZeroed(ComRef);
};


// SRef class automatically deallocated when destructed (like Ref with count==1)

template<class Type>
class InitPtr
{
  private:
  mutable Type *_ref;
  // we use mutable because Visual C++ 5.0 has bug
  // - sometime const / non-const reference is messed up in templates

  public:
  //! make data type accessible
  typedef Type ItemType;

  InitPtr(){_ref=NULL;}
  InitPtr( Type *source ){_ref=source;}
  void operator = ( Type *source ){_ref=source;}
  
  bool NotNull() const {return _ref!=NULL;}
  bool IsNull() const {return _ref==NULL;}
  bool operator ! () const {return _ref==NULL;}
  //operator bool() const {return _ref!=NULL;}

  Type *operator -> () const {return _ref;}
  operator Type *() const {return _ref;} // casting
  Type *GetRef() const {return _ref;}
  Type **GetRefPtr() const {return &_ref; }

  ClassIsSimpleZeroed(InitPtr)
};

/// provide delete for a normal pointer
template <class Type>
struct SRefTraits
{
  static void Delete(Type *ptr) {delete ptr;}
};

/// provide delete for an array pointer
template <class Type>
struct SRefArrayTraits
{
  static void Delete(Type *ptr) {delete[] ptr;}
};


//! This template class provides a little pointer superstructure.
/*!
  Whereas using Ref structure more pointers can share one object, this structure is
  suitable just for one pointer - one object relation. Anyway you can be sure that
  with destroying the pointer the object itself will be also destroyed.
  \sa Ref
*/
template<class Type, class Traits=SRefTraits<Type> >
class SRef
{
protected:
  //! Reference to the object.
  /*!
    Note that we use the mutable keyword because Visual C++ 5.0 has a bug
    - sometimes const/non-const reference is messed up in templates.
  */
  mutable Type *_ref;
public:
  //! Default constructor
  __forceinline SRef(){_ref=NULL;}
  //! Using this constructor you can determine the object to point to.
  /*!
    \param source Pointer to the object to be assigned.
  */
  __forceinline SRef( Type *source )
  {
    _ref=source;
  }
  //! Assigning of an object. The previous content will be deleted.
  void operator = ( Type *source )
  {
    if( _ref==source ) return;
    Free();
    _ref=source;
  }
  //! Copy constructor
  /*!
    \param sRef Reference to the source pointer. The source will be invalid then.
  */
  SRef( const SRef &source )
  {
    _ref=source._ref;
    source._ref=NULL; // old is invalidated, valid is only new copy
  }
  //! Assigning of pointers. The one from the right side will be invalid then.
  void operator = ( const SRef &source )
  {
    if( source._ref!=_ref )
    {
      Free();
      _ref=source._ref;
      source._ref=NULL; // old is invalidated, valid is only new copy
    }
  }
  //! Determining whether pointer is not null.
  /*!
    \return TRUE in case pointer is not null, FALSE otherwise.
  */
  __forceinline bool NotNull() const {return _ref!=NULL;}
  //! Determining whether pointer is null.
  /*!
    \return TRUE in case pointer is null, FALSE otherwise.
  */
  __forceinline bool IsNull() const {return _ref==NULL;}
  //! Determining whether pointer is null.
  __forceinline bool operator ! () const {return _ref==NULL;}
  //! Destructor will delete the object itself
  __forceinline ~SRef(){Free();}
  //! This method will delete the object.
  void Free(){if( _ref ) Traits::Delete(_ref),_ref=NULL;}
  //! This operator is suitable for accessing object members. It has the same meaning like standard -> operator.
  __forceinline Type *operator -> () const {return _ref;}
  //! Operator for type casting.
  __forceinline operator Type *() const {return _ref;}
  //! Getting pointer by standard method (if all other ways to get pointer fails)
  __forceinline Type *GetRef() const {return _ref;}

  //! Unlinks pointer. Gives value away to prevend deallocating when destruction.
  Type *Unlink() {Type *out=_ref;_ref=0;return out;}

  ClassIsMovableZeroed(SRef);
};

//! This template class provides a little pointer superstructure.
/*!
  Similar to SRef, but pointer is assumed to point to an array of objects.
  delete[] is used for destruction.
  \sa Ref
*/
template<class Type, class Traits=SRefArrayTraits<Type> >
class SRefArray: public SRef<Type,Traits>
{
private:
  typedef SRef<Type,Traits> base;
  
  // we can initialize using new, or using encapsulated functions
  void Alloc( int n )
  {
    #if ALLOC_DEBUGGER && defined _CPPRTTI
      base::_ref=new(FileLine(typeid(Type).name()),sizeof(Type)) Type[n];
    #else
      base::_ref=new Type[n];
    #endif
  }
  
public:
  //! Default constructor
  __forceinline SRefArray(){}
  //! Using this constructor you can determine the object to point to.
  /*!
    \param source Pointer to the object to be assigned.
  */
  __forceinline SRefArray( Type *source )
  :base(source)
  {
  }
  explicit SRefArray(int n)
  {
    Alloc(n);
  }

  void Realloc( int n )
  {
    base::Free();
    Alloc(n);
  }
  Type *Data() {return base::_ref;} 
  const Type *Data() const {return base::_ref;} 
  
  ClassIsMovableZeroed(SRefArray);
};


/// compatibility wrapped for SRefArray
template<class Type>
class APtr: public SRefArray<Type>
{
  private:
  typedef SRefArray<Type> base;

  public:
  APtr(){base::_ref=NULL;}
  explicit APtr( int n )
  :base(n)
  {
  }
  
  ClassIsMovableZeroed(APtr);
};

#define REF_MT_SAFE 1

#if _DEBUG
# define CHECK_REFCOUNT_MT 1
#else
# define CHECK_REFCOUNT_MT 0
#endif

//! This class as well as Ref template class are designed to provide smart pointer operations.
/*!
  Using this classes you need not take care of releasing structures you have allocated.
  There is a reference count attribute inside RefCount class (_count) which helps us to determine wheter there still exists some valid pointer to it.
  In case there is not we will delete whole object without fear.

  Note that at the end of the scope where pMO* is defined, the object will be automatically released.
  Another note that you MUST use the new keyword when allocating the space for the object.
  Otherwisely constructor would not be called and _count would not be initialized.
  \sa Ref
*/

template <bool mtSafe>
class RefCountBaseT
{
private:
  //! Number of references to this object.
  mutable int _count;

public:
  //! Default constructor.
  RefCountBaseT(){_count=0;}
  //! Copy constructor (_count attribute zeroed).
  /*!
    \param src Reference to source object.
  */
  RefCountBaseT( const RefCountBaseT &src ){_count=0;}
  //! Copying of object (_count attribute of the target is left intact - target already exists).
  void operator =( const RefCountBaseT &src ){}
  //! Destructor
public:
  //! Adding of reference to this object.
  /*!
    \return Number of references.
  */
  __forceinline int AddRef() const
  {
#if CHECK_REFCOUNT_MT
    AssertMainThread();
#endif
    return ++_count;
  }
  //! Determines number of references to this object.
  /*!
    \return Number of references.
  */

  //! Releasing of reference to this object.
  /*!
    \return Number of references.
  */
  __forceinline int DecRef() const
  {
#if CHECK_REFCOUNT_MT
    AssertMainThread();
#endif
    int ret =--_count;
    Assert(ret>=0);
    return ret;
  }
  /// check reference count
  __forceinline int RefCounter() const {return _count;}
};


/// specialize the MT safe variant
template <>
class RefCountBaseT<true>
{
private:
  //! Number of references to this object.
  volatile mutable long _count;

public:
  //! Default constructor.
  RefCountBaseT(){_count=0;}
  //! Copy constructor (_count attribute zeroed).
  /*!
    \param src Reference to source object.
  */
  RefCountBaseT( const RefCountBaseT &src ){_count=0;}
  //! Copying of object (_count attribute of the target is left intact - target already exists).
  void operator = ( const RefCountBaseT &src ){}
public:
  //! Adding of reference to this object.
  /*!
    \return Number of references.
  */
  __forceinline int AddRef() const {return InterlockedIncrementAcquire(&_count);}

  //! Releasing of reference to this object.
  /*!
    \return Number of references.
  */
  __forceinline int DecRef() const
  {
    int ret = InterlockedDecrementRelease(&_count); //flush all writes
    Assert(ret>=0);
    return ret;
  }
  /// Determines number of references to this object.
  /*!
    \return Number of references.
  */
  __forceinline int RefCounter() const {return _count;}
};

#if REF_MT_SAFE
const bool RefMTSafe = true;
#else
const bool RefMTSafe = false;
#endif


//! This class as well as Ref template class are designed to provide smart pointer operations.
/*!
This class is potentially unsafe, as there are no virtual functions used.
When using it, you need to make sure pointer to relevant child is always used.
*/

class RefCountBase: public RefCountBaseT<RefMTSafe>
{
};


//! This class as well as Ref template class are designed to provide smart pointer operations.
/*!
  Using this classes you need not take care of releasing structures you have allocated.
  There is a reference count attribute inside RefCount class (_count) which helps us to determine whether there still exists some valid pointer to it.
  In case there is not we will delete whole object without fear.

  Note that at the end of the scope where pMO* is defined, the object will be automatically released.
  Another note that you MUST use the new keyword when allocating the space for the object.
  Otherwisely constructor would not be called and _count would not be initialized.
  \sa Ref
*/
class RefCount: public RefCountBaseT<RefMTSafe>
{
  typedef RefCountBaseT<RefMTSafe> base;
public:
  virtual ~RefCount(){} // use destructor of derived class
  //! Adding of reference to this object.
  /*!
    \return Number of references.
  */
  #if _DEBUG
  /// assert/debugging opportunity - verify object state is correct when used
  /**
   Debugging only - Never called in release build.
  */
  virtual void OnUsed() const {}
  __forceinline int AddRef() const
  {
    if (RefCounter()==0) OnUsed();
    return base::AddRef();
    }
#endif
  //! Determines number of references to this object.
  /*!
    \return Number of references.
  */

  //! Releasing of reference to this object.
  /*!
    \return Number of references.
  */
  __forceinline int Release() const
  {
    int ret = base::DecRef();
    if( ret==0 ) OnUnused();
    return ret;
  }
  /// destroy an object using delete
  void Destroy() const
  {
    delete const_cast<RefCount *>(this);
  }
  /// called when object is no longer used and may be released
  /**
  Default implementation calls delete
  Typical override may perform some caching
  */
  virtual void OnUnused() const {Destroy();}
  /// get memory used by this object
  /**
  Does not include memory used to hold this object itself,
  which is already covered by sizeof
  */
  virtual double GetMemoryUsed() const {return 0;}
  
  protected:
  //@{ pretend the shape is still loaded during its unloading to avoid destructor recursion
  void TempAddRef() {base::AddRef();}
  void TempRelease() {base::DecRef();}
  //@}
};

//! This template class provides a smart pointers.
/*!
  This template class provides a reference substitution for smart pointer operations.
  Type must have members AddRef and Release defined. This can be accomplished
  by deriving from RefCount class.
  \sa RefCount, SRef, ComRef
*/
template<class Type>
class Ref
{
protected:
  //! Reference to the object.
  Type *_ref;
public:
  //! make data type accessible
  typedef Type ItemType;
  //! Default constructor.
  __forceinline Ref(){_ref=NULL;}
  // Using this constructor you can get the existing reference from source parameter.
  /*!
    \param source Source pointer to an object.
  */
  Ref( Type *source )
  {
    if( source ) source->AddRef();
    _ref=source;
  }
  //! Copying of pointer.
  const Ref<Type> &operator = ( Type *source )
  {
    Type *old = _ref; 
    if( source ) source->AddRef();
    // for MT safety of shared Ref objects we need to stop pointing to the object before we release the last ref to it
    _ref=source;
    if( old ) old->Release();
    return *this;
  }
  // Copy constructor.
  /*!
    \param sRef Reference to the source pointer.
  */
  Ref( const Ref &sRef )
  {
    Type *source=sRef._ref;
    if( source ) source->AddRef();
    _ref=source;
  }
  //! Copying of pointer.
  const Ref<Type> &operator = ( const Ref &sRef )
  {
    Type *source = sRef._ref;
    Type *old = _ref; 
    if( source ) source->AddRef();
    // for MT safety of shared Ref objects we need to stop pointing to the object before we release the last ref to it
    _ref=source;
    if( old ) old->Release(); // Release will do Publish as well (part of InterlockedDecrementRelease)
    return *this;
  }
  //! Determining whether pointer is not null.
  /*!
    \return TRUE in case pointer is not null, FALSE otherwise.
  */
  __forceinline bool NotNull() const {return _ref!=NULL;}
  //! Determining whether pointer is null.
  /*!
    \return TRUE in case pointer is null, FALSE otherwise.
  */
  __forceinline bool IsNull() const {return _ref==NULL;}
  //! Destructor - heart of automatic releasing.
  __forceinline ~Ref(){Free();}
  //! This method will release the object (or at least decrements it's reference counter).
  /*!
    However you are not forced to use it. Object will release itself automatically.
  */
  void Free(){if( _ref ) _ref->Release(),_ref=NULL;}
  //! This method will return standard pointer to the object.
  /*!
    \return Standard pointer to the object.
  */
  __forceinline Type *GetRef() const {return _ref;}
  //! This method will set reference to the object.
  /*!
    You must use this function with infernal caution. For instance if you set it carelessly to NULL,
    you can be sure that the previous object will never be deleted.
    \param ref New pointer to the object.
  */
  __forceinline void SetRef( Type *ref ) {_ref=ref;}
  //! This operator is suitable for accessing object members. It has the same meaning like standard -> operator.
  __forceinline Type *operator -> () const {return _ref;}
  //! Operator for type casting.
  __forceinline operator Type *() const {return _ref;}

  //! calculate memory used
  double GetMemoryUsed() const
  {
    // for each owner only appropriate part of shared object is considered
    if (!_ref) return 0;
    return double(sizeof(Type)+_ref->GetMemoryUsed())/_ref->RefCounter();
  }
  ClassIsMovableZeroed(Ref);
};

template<class Type>
struct DefRefTraits
{
  static __forceinline int AddRef(Type *ptr) {return ptr->AddRef();}
  static __forceinline int Release(Type *ptr) {return ptr->Release();}
};

//! This template class provides a smart pointers.
/*!
  This template class provides a reference substitution for smart pointer operations.
  Type must have members AddRef and Release defined. This can be accomplished
  by deriving from RefCount class.
  If you want to avoid having class Type declared everywhere where Ref<Type> copy or destrcutor
  is used, you can specify which type is used as storage type. If Type is derived from RefCount,
  you can always provide RefCount as StoreAs.
  \sa RefCount, SRef, ComRef
*/
template<class Type,class StoreAs=RefCount,class Traits=DefRefTraits<StoreAs> >
class RefR
{
protected:
  //! Reference to the object.
  StoreAs *_ref;
public:
  //! make data type accessible
  typedef Type ItemType;
  //! Default constructor.
  __forceinline RefR(){_ref=NULL;}
  // Using this constructor you can get the existing reference from source parameter.
  /*!
    \param source Source pointer to an object.
  */
  RefR( Type *source )
  {
    if( source ) Traits::AddRef(source);
    _ref=source;
  }
  //! Copying of pointer.
  void operator = ( Type *source )
  {
    if( source ) Traits::AddRef(source);
    if( _ref ) Traits::Release(_ref);
    _ref=source;
  }
  // Copy constructor.
  /*!
    \param sRef Reference to the source pointer.
  */
  RefR( const RefR &sRef )
  {
    StoreAs *source=sRef._ref;
    if( source ) Traits::AddRef(source);
    _ref=source;
  }
  //! Copying of pointer.
  void operator = ( const RefR &sRef )
  {
    StoreAs *source=sRef._ref;
    if( source ) Traits::AddRef(source);
    if( _ref ) Traits::Release(_ref);
    _ref=source;
  }
  //! Determining whether pointer is not null.
  /*!
    \return TRUE in case pointer is not null, FALSE otherwise.
  */
  __forceinline bool NotNull() const {return _ref!=NULL;}
  //! Determining whether pointer is null.
  /*!
    \return TRUE in case pointer is null, FALSE otherwise.
  */
  __forceinline bool IsNull() const {return _ref==NULL;}
  //! Destructor - heart of automatic releasing.
  __forceinline ~RefR(){Free();}
  //! This method will release the object (or at least decrements it's reference counter).
  /*!
    However you are not forced to use it. Object will release itself automatically.
  */
  void Free(){if( _ref ) Traits::Release(_ref),_ref=NULL;}
  //! This method will return standard pointer to the object.
  /*!
    \return Standard pointer to the object.
  */
  __forceinline Type *GetRef() const {return static_cast<Type *>(_ref);}
  //! This method will set reference to the object.
  /*!
    You must use this function with infernal caution. For instance if you set it carelessly to NULL,
    you can be sure that the previous object will never be deleted.
    \param ref New pointer to the object.
  */
  __forceinline void SetRef( Type *ref ) {_ref=ref;}
  //! This operator is suitable for accessing object members. It has the same meaning like standard -> operator.
  __forceinline Type *operator -> () const {return static_cast<Type *>(_ref);}
  //! Operator for type casting.
  __forceinline operator Type *() const {return static_cast<Type *>(_ref);}

  //! calculate memory used
  double GetMemoryUsed() const
  {
    // for each owner only appropriate part of shared object is considered
    Type *ref = static_cast<Type *>(_ref);
    return double(sizeof(Type)+ref->GetMemoryUsed())/_ref->RefCounter();
  }
  ClassIsMovableZeroed(RefR);
};

//! Segment of alternative NULL addresses: 0 to MaxNull-1
#define MaxNull 0x0004

#define DNull(x) ((unsigned)(x) < MaxNull)

/*!
  The same as Ref<> but uses alternative NULL addresses (0-3) and
  re-initializes reference after destruction..
  \patch 27.3.2002 Pepca (alternative NULL values, DNull())
*/
template < class Type >
class RefD
{
protected:
    //! Reference to the object.
    Type *_ref;
public:
  //! make data type accessible
  typedef Type ItemType;
    //! Default constructor.
    __forceinline RefD ()
    { _ref=NULL; }
    // Using this constructor you can get the existing reference from source parameter.
    /*!
      \param source Source pointer to an object.
    */
    RefD ( Type *source )
  {
    if ( !DNull(source) ) source->AddRef();
    _ref=source;
  }
    /*! Copy constructor.
        \param sRef Reference to the source pointer.
    */
    RefD ( const RefD &sRef )
  {
        Type *source = sRef._ref;
        if( !DNull(source) ) source->AddRef();
        _ref=source;
    }
    /*!
        Cross-constructor from Ref<>.
    */
    RefD ( const Ref<Type> &sRef )
    {
        Type *source = sRef.GetRef();
        if( !DNull(source) ) source->AddRef();
        _ref=source;
    }
    //! Copying of pointer.
    void operator = ( Type *source )
    {
        if ( !DNull(source) ) source->AddRef();
        if ( !DNull(_ref) ) _ref->Release();
        _ref=source;
    }
    //! Copying of pointer.
    void operator = ( const RefD &sRef )
    {
        Type *source = sRef._ref;
        if ( !DNull(source) ) source->AddRef();
        if ( !DNull(_ref) ) _ref->Release();
        _ref=source;
    }
    //! Comparison with pointer.
    __forceinline bool operator == ( const Type *source ) const
    {
        return _ref == source;
    }
    //! Comparison with pointer.
    __forceinline bool operator != ( const Type *source ) const
    {
        return _ref != source;
    }
    //! Comparison with Ref<>.
    __forceinline bool operator == ( const Ref<Type> &sRef ) const
    {
        return _ref == sRef.GetRef();
    }
    //! Comparison with Ref<>.
    __forceinline bool operator != ( const Ref<Type> &sRef ) const
    {
        return _ref != sRef.GetRef();
    }
    //! Comparison with RefD<>.
    __forceinline bool operator == ( const RefD<Type> &sRef ) const
    {
        return _ref == sRef._ref;
    }
    //! Comparison with RefD<>.
    __forceinline bool operator != ( const RefD<Type> &sRef ) const
    {
        return _ref != sRef._ref;
    }
    //! bool typecast.
  __forceinline operator bool() const
    {
        return !DNull(_ref);
    }
    //! bool typecast.
  __forceinline bool operator ! () const
    {
        return DNull(_ref);
    }
    //! This operator is suitable for accessing object members. It has the same meaning like standard -> operator.
    __forceinline Type *operator -> () const
    {
        return _ref;
    }
    //! Operator for type casting.
    __forceinline operator Type *() const
    {
        return _ref;
    }
    //! This method will release the object (or at least decrements it's reference counter).
    /*!
      However you are not forced to use it. Object will release itself automatically.
    */
    void Free()
    {
        if ( !DNull(_ref) ) {
            _ref->Release();
            _ref=NULL;
            }
    }
    //! Destructor - heart of automatic releasing.
    ~RefD ()
    {
        Free();
    }
    /*!
      \return TRUE in case pointer is not null, FALSE otherwise.
    */
    __forceinline bool NotNull() const
    {
        return !DNull(_ref);
    }
  //! Determining wheter pointer is null.
    /*!
      \return TRUE in case pointer is null, FALSE otherwise.
    */
    __forceinline bool IsNull() const
    {
        return DNull(_ref);
    }
    //! This method will return standard pointer to the object.
    /*!
      \return Standard pointer to the object.
    */
    __forceinline Type *GetRef() const
    {
        return _ref;
    }
  ClassIsMovableZeroed(RefD);
};

template<class Type>
class RefN
{
public:
  static RefCount _nil;
  #define TypeNull ((Type *)&_nil)

private:
  Type *_ref;

public:
  //! make data type accessible
  typedef Type ItemType;

  __forceinline RefN(){_ref=TypeNull;}
  
  RefN( Type *source )
  {
    if( !source )
    {
      // TODO: avoid assigning NULL
      source=TypeNull;
    }
    source->AddRef();
    _ref=source;
  }
  void operator = ( Type *source )
  {
    if( !source )
    {
      // TODO: avoid assigning NULL
      source=TypeNull;
    }
    source->AddRef();
    _ref->Release();
    _ref=source;
  }
  RefN( const RefN &sRef )
  {
    Type *source=sRef._ref;
    source->AddRef();
    _ref=source;
  }
  void operator = ( const RefN &sRef )
  {
    Type *source=sRef._ref;
    source->AddRef();
    _ref->Release();
    _ref=source;
  }
  
  __forceinline bool NotNull() const {return _ref!=TypeNull;}
  __forceinline bool IsNull() const {return _ref==TypeNull;}

  ~RefN(){if( _ref ) _ref->Release();}
  void Free(){operator =(TypeNull);}

  // note: may return TypeNull
  __forceinline Type *GetRef() const {return _ref;}
  __forceinline void SetRef( Type *ref ) {_ref=ref;} // note: use with caution

  __forceinline Type *operator -> () const {return _ref;}
  __forceinline operator Type *() const {return _ref;} // casting

  ClassIsMovableZeroed(RefN);
};

template <class Type>
RefCount RefN<Type>::_nil;

/*
// TODO: use RefN instead of Ref
#define Ref RefN
*/

// Temp class to automate deleting of dynamic objects


//! This class provides an array structure with deleting dynamic object automation.
/*!
  It is sort of similar to AutoArray class, however, it is not so advanced.
  \sa AutoArray
*/
template <class Type>
class Temp
{
  typedef ConstructTraits<Type> CTraits;
private:
  //! Pointer to data.
  Type *_obj;
  //! Number of items in the array.
  int _n;
  //! Allocation of space for n items, implicit constructors are not called.
  /*!
    \param n Number of items to allocate space for.
  */
  void DoAlloc( int n )
  {
    #if ALLOC_DEBUGGER && defined _CPPRTTI
      char *mem=new(FileLine(typeid(Type).name()),sizeof(Type)) char[n*sizeof(Type)];
    #else
      char *mem=new char[n*sizeof(Type)];
    #endif
    _obj=(Type *)mem;
    _n=n;
  }
  //! Allocation of space for n items, implicit constructors are called.
  /*!
    \param n Number of items to allocate space for.
  */
  void DoConstruct( int n )
  {
    DoAlloc(n);
    CTraits::ConstructArray(_obj,_n);
  }
  //! Deallocation of allocated space, destructors are called.
  void DoDelete()
  {
    if( _obj )
    {
      CTraits::DestructArray(_obj,_n);
      delete[] (char *)_obj;
      _obj=NULL;
    }
  }
public:
  //! make data type accessible
  typedef Type ItemType;

  //! Default constructor.
  Temp(){_obj=NULL,_n=0;}
  //! Using this constructor you can allocate space for n items. Implicit constructors of those items are called.
  /*!
    \param n Number of items to allocate space for.
  */
  explicit Temp( int n ){DoConstruct(n);}
  //! Using this constructor you can create an array with n items.
  //! For each item will be called copy constructor with correspond src item.
  /*!
    \param src Source array.
    \param n Number of items to allocate space for.
  */
  Temp( const Type *src, int n )
  {
    DoAlloc(n);
    CTraits::CopyConstruct(_obj,src,_n);
  }
  //! Assigning of an array. Array will be copied, the previous one will be deleted.
  //! For each item will be called copy constructor with correspond src item.
  void operator = ( const Temp &src )
  {
    DoDelete();
    DoAlloc(src.Size());
    CTraits::CopyConstruct(_obj,src.Data(),src.Size());
  }
  //! Copy constructor. Array will be copied, the previous one will be deleted.
  //! For each item will be called copy constructor with correspond src item.
  /*!
    \param src Source array.
  */
  Temp( const Temp &src )
  {
    DoAlloc(src.Size());
    CTraits::CopyConstruct(_obj,src.Data(),src.Size());
  }
  //! This method will export data of this object.
  /*!
    The data pointer of this object will be cancelled. It is up to caller of this method to delete data.
    \return Pointer to data
  */
  Type *Export() {Type *ret=_obj;_obj=NULL;return ret;}
  //! Determining wheter pointer is not null.
  /*!
    \return TRUE in case pointer is not null, FALSE otherwise.
  */
  __forceinline bool NotNull() const {return _obj!=NULL;}
  //! Determining wheter array is null.
  /*!
    \return TRUE in case array is null, FALSE otherwise.
  */
  __forceinline bool IsNull() const {return _obj==NULL;}
  //! Determining wheter array is null.
  __forceinline bool operator ! () const {return _obj==NULL;}
  //! Destructor will delete the array itself
  __forceinline ~Temp(){DoDelete();}
  //! This method will delete the array.
  __forceinline void Free(){DoDelete();}
  //! Operator for type casting.
  __forceinline operator Type *() {return _obj;}
  //! Operator for type casting.
  __forceinline operator const Type *() const {return _obj;}
  //! This method returns pointer to array.
  /*!
    \return Pointer to array.
  */
  __forceinline Type *Data() {return _obj;}
  //! This method returns pointer to array.
  /*!
    \return Pointer to array.
  */
  __forceinline const Type *Data() const {return _obj;}
  //! Returns number of items in the array.
  /*!
    \return Number of items in the array.
  */
  __forceinline int Size() const {return _n;}
  //! Setting of specified item.
  /*!
    Returned item can be modified.
    \param i Index to desirable item.
    \return Desirable item
  */
  Type &Set( int i )
  {
    AssertDebug( i>=0 && i<_n );
    return _obj[i];
  }
  //! Getting of specified item.
  /*!
    Returned item cannot be modified.
    \param i Index to desirable item.
    \return Desirable item.
  */
  const Type &Get( int i ) const
  {
    AssertDebug( i>=0 && i<_n );
    return _obj[i];
  }
  //! Defined operator [] for modifying a value.
  Type &operator [] ( int i ) {return Set(i);}
  //! Defined operator [] for reading a value.
  const Type &operator [] ( int i ) const {return Get(i);}
  //! Reallocating of the array to n items. The previous array will be deleted.
  /*!
    \param n Number of items in the new array.
  */
  void Realloc( int n )
  {
    DoDelete();
    DoConstruct(n);
  }
  //! Reallocating of the array to n items. The previous array will be deleted.
  //! The new array will be initialized by copy constructor with particular src parameter.
  /*!
    \param src Source array.
    \param n Number of items in the new array.
  */
  void Realloc( const Type *src, int n )
  {
    DoDelete();
    DoAlloc(n);
    CTraits::CopyConstruct(_obj,src,_n);
  }

  ClassIsMovable(Temp);
};

template <class Type>
class Auto
{
  typedef ConstructTraits<Type> CTraits;
  private:
  Type *_obj;
  int _n;

  private: // No copy
  void operator = ( const Auto &src );
  Auto( const Auto &src );
  
  public:
  Auto( int n, void *mem )
  {
    _obj=(Type *)mem;
    _n=n;
    CTraits::ConstructArray(_obj,_n);
  }
  ~Auto()
  {
    CTraits::DestructArray(_obj,_n);
  }

  operator Type *() {return _obj;} // casting
  operator const Type *() const{return _obj;} // casting

  Type *Data() {return _obj;}
  const Type *Data() const {return _obj;}
  int Size() const {return _n;}
};

#define AutoAuto(Type,var,n) Auto<Type> var(n,_alloca(sizeof(Type)*n))

//! default implementation of GetMemoryUsed returns 0 - no overhead besides of sizeof
template <class Type>
struct GetMemoryUsedTraits
{
  static double GetMemoryUsed(const Type &src)
  {
    return 0;
  }
};

// class to automate buffer deallocating and size information
template <class Type>
class Buffer: public RefCount
{
  typedef ConstructTraits<Type> CTraits;

  private:
  size_t _length;
  Type *_buffer;
  
  protected:
  void DoConstruct(){_buffer=NULL,_length=0;}
  void DoAlloc( size_t i )
  {
    #if ALLOC_DEBUGGER && defined _CPPRTTI      
      _buffer=(Type *) new(FileLine(typeid(Type).name()),sizeof(Type)) char[i*sizeof(Type)];
    #else
      _buffer=(Type *) new char[i*sizeof(Type)];
    #endif
    _length=i;
  }

  void DoConstruct( size_t i )
  {
    DoAlloc( i );
    CTraits::ConstructArray(_buffer,_length);
  }

  void DoConstruct( const Type *src, size_t len )
  {
    DoAlloc(len);
    CTraits::CopyConstruct(_buffer,src,_length);
  }

  void DoConstruct( const Buffer &src, int from=0, int to=-1 )
  {
    if( src._buffer )
    {
      if( to<0 ) to=src._length;
      DoConstruct(src._buffer+from,to-from);
    }
    else
    {
      _buffer=NULL,_length=0;
    }
  }
  
  public:
  void Delete()
  {
    if( _buffer )
    {
      CTraits::DestructArray(_buffer,_length);
      delete[] (char *)_buffer;
      _buffer = NULL;
      _length = 0;
    }   
  }
  // AutoArray style access
  typedef Type DataType;
  // AutoArray style access
  void Clear(){Delete();}

  Buffer(){DoConstruct();}
  Buffer( size_t len ){DoConstruct(len);}
  Buffer( const Type *src, size_t len ) {DoConstruct(src,len);}
  Buffer( const Buffer &src, int from=0, int to=-1 ){DoConstruct(src,from,to);}
  void Init() {Delete();DoConstruct();}
  void Init( int len ) {Delete();DoConstruct(len);}
  void Init( const Type *src, int len ) {Delete();DoConstruct(src,len);}
  void Realloc(int len) {}
  void Resize(int len)
  {
    if (_length!=len) Delete(),DoConstruct(len);
  }
  void Init( const Buffer &src, int from=0, int to=-1 )
  {
    Delete();
    DoConstruct(src,from,to);
  }
  ~Buffer(){Delete();}
  
  void operator = ( const Buffer &src )
  {
    if (this==&src) return;
    Delete();
    DoConstruct(src);
  }
  
  double GetMemoryUsed() const
  {
    double res = _length*sizeof(Type);
    // we cannot assume items have GetMemoryUsed member
    for (int i=0; i<int(Size()); i++)
    {
      res += GetMemoryUsedTraits<Type>::GetMemoryUsed((*this)[i]);
    }
    return res;
  }
  size_t GetMemoryAllocated() const
  {
    return _length*sizeof(Type);
  }

  __forceinline size_t Length() const {return _length;}
  __forceinline size_t Size() const {return _length;}
  __forceinline Type *Data() {return _buffer;}
  __forceinline const Type *Data() const {return _buffer;}
  __forceinline operator Type *() {return _buffer;} // casting
  __forceinline operator const Type *() const {return _buffer;} // casting

  const Type &operator []( int i ) const
  {
    Assert(i>=0 && (size_t)i<_length);
    return _buffer[i];
  }
    
  Type &operator []( int i)
  {
    Assert(i>=0 && size_t(i)<_length);
    return _buffer[i];
  }

  ClassIsMovable(Buffer);
};


#include <Es/Memory/debugNew.hpp>


#endif

