#ifndef _ES_listBiDir_hpp_
#define _ES_listBiDir_hpp_

#include <Es/Framework/debugLog.hpp>

//! Base class for defining elements of TListBidir
/*!
  This structure allows deletion of specified element in time o(1).
  Note that int template will allow multiple inheritance for a single
  object.
  \patch_internal 1.45 Date 2/15/2002 by Ondra
  - Fixed: More robust handling of TLinkBidir destrution
  when still in list.
*/
template <int i=0> class TLinkBidir: private NoCopy
{
protected:
  //! Resets pointers (usually used at the time of creating a list)
	void Reset() {prev=next=this;}
public:
  typedef TLinkBidir PrevNextType;
  //! Pointer to the previous item in the list
	TLinkBidir *prev;
  //! Pointer to the next item in the list
	TLinkBidir *next;
	//! Constructor sets pointers to NULL
	TLinkBidir(){prev=next=NULL;}
  //! Destructor safely removes item from the list
	~TLinkBidir()
	{
		if (prev || next)
		{
			Assert(prev);
			Assert(next);
			Delete();
		}
	}
  //! Removes item from the list
	void Delete()
	{
		// check it is in list
		Assert( prev );
		Assert( next );
		// remove element from list

		next->prev=prev;
		prev->next=next;
	
		// not linked
		prev=next=NULL;
	}
  //! Determines whether item is in list or not
  /*!
    \return Boolean value determines whether the item is in the list or not
  */
	bool IsInList() const {return prev!=NULL;}
  //! Swaps item in the list with the next one
  void SwapWithNext()
  {
    TLinkBidir *pTempNext = next->next;
    TLinkBidir *pTempPrev = prev;
    next->next = this;
    next->prev = pTempPrev;
    prev = next;
    next = pTempNext;
    pTempNext->prev = this;
    pTempPrev->next = prev;  
  };
};

typedef TLinkBidir<0> TLinkBidirD;

//! no counting for EmptyCounter
template <class Type>
class EmptyCounter
{
	//int _dummy;

	public:
	void AddedItem(const Type *item) {}
	
	void RemovedItem(const Type *item) {}
	
	static bool IsCounted() {return false;}
	int GetCount() const {Fail("Not possible");return 0;}
	void operator += (const EmptyCounter &src){}
};

//! element counting for EmptyCounter
template <class Type>
class SimpleCounter
{
	int _count;

	public:
	SimpleCounter()
	{
		_count = 0;
	}

	void AddedItem(const Type *item)
	{
		_count++;
	}
	void RemovedItem(const Type *item)
	{
		_count--;
	}
	static bool IsCounted() {return true;}
	int GetCount() const {return _count;}

	void operator += (const SimpleCounter &src)
	{
		_count += src._count;
	}
};

//! Bidirectional link list
/*!
  Note that you can choose any object inherited from TLinkBidir to be the
  head. By default it is the class TLinkBidir itself.
	CounterType argument provides means to maintain total element count, total size or similar properties
*/
template <class Type, class Base=TLinkBidirD, class CounterType=EmptyCounter<Type> >
class TListBidir: public Base
{
	//! element totals - may be empty
	CounterType _counter;
private:
  //! Copy constructor will be disabled
	TListBidir( const TListBidir &src );
  //! Copy operator will be disabled
	void operator = ( const TListBidir &src );
protected:
  //! Resets the list - the list will be empty
	void Reset(){Base::Reset();}
public:
	//! provide a way to query datatype
	typedef Type ItemType;
  //! Constructor
	TListBidir(){Reset();}
  //! Destructor will remove each item from the queue
  /*!
    Note that items will not be deleted.
  */
	~TListBidir()
	{
		Clear();
		// prepare for TLinkBidir destruction
		Assert (Base::prev==this);
		Assert (Base::next==this);
		Base::prev = Base::next = NULL;
	}
  //! Inserts specified item at the first place in the list
  /*!
    \param element Item to insert
  */
	void Insert( Type *element )
	{
		// verify element is not in any list
		Assert(!element->Base::prev);
		Assert(!element->Base::next);
		// insert before first element

		Base::next->prev=element;
		element->Base::next=Base::next;
		
		element->Base::prev=this;
		Base::next=element;
		_counter.AddedItem(element);
	}
  //! Inserts specified item at the last place in the list
  /*!
    \param element Item to add
  */
	void Add( Type *element )
	{
		// verify element is not in any list
		Assert(!element->Base::prev);
		Assert(!element->Base::next);
		// insert after the last element
		Base::prev->Base::next=element;
		element->Base::prev=Base::prev;
		
		element->Base::next=this;
		Base::prev=element;
		_counter.AddedItem(element);
	}
  //! Inserts one item before another
  /*!
    \param after This item will be after the inserted item in the list
    \param element This item we are about to insert
  */
	void InsertBefore( Type *after, Type *element )
	{
		// verify element is not in any list
		Assert(!element->Base::prev);
		Assert(!element->Base::next);
		// insert before the element
		after->Base::prev->Base::next=element;
		element->Base::prev=after->Base::prev;
		
		element->Base::next=after;
		after->Base::prev=element;
		_counter.AddedItem(element);
	}
  //! Inserts one item after another
  /*!
    \param before This item will be before the inserted item in the list
    \param element This item we are about to insert
  */
	void InsertAfter( Type *before, Type *element )
	{
		// verify element is not in any list
		Assert(!element->Base::prev);
		Assert(!element->Base::next);
		// insert after the element
		before->Base::next->Base::prev=element;
		element->Base::next=before->Base::next;
		
		element->Base::prev=before;
		before->Base::next=element;
		_counter.AddedItem(element);
	}
  //! Removes specified item from the queue
  /*!
    \param element Item to delete
  */
	void Delete( Type *element )
	{
    Assert(element->Base::IsInList()); // Deleting item that is not in the list will lead to counter desynchronization and is quite suspicious in general
		element->Base::Delete();
		_counter.RemovedItem(element);
	}

  /// brute force search to check if the item is in the list
  /** using element->IsInList() is much faster and should be preferred, unless the item can be member of several lists */
  bool Contains(Type *element)
  {
    for(Type *i=Start(); NotEnd(i); i=Advance(i))
    {
      if (i==element) return true;
    }
    return false;
  }
  //! Returns first item in the list or the head if there isn't any
  /*!
    This method and Advance and NotEnd methods provide fast way
    to iterate through the items in the list.
    Note that the dangerousness results from the fact that
    the returning item need not be of type Type (the head itself for
    example will be usually of type TLinkBidir).
    Example of loop: for(Type *i=list.Start(); list.NotEnd(i); i=list.Advance(i)) {}
    \return First item in the list or head
    \sa End, Advance, Retreat, NotEnd
  */
	Type *Start() const
	{
		return static_cast<Type *>(Base::next);
	}
  //! Returns last item in the list or the head if there isn't any
  /*!
    \return Last item in the list or head
    \sa Start, Advance, Retreat, NotEnd
  */
  Type *End() const
  {
    return static_cast<Type *>(Base::prev);
  }
  //! Returns next item in the list
  /*!
    \param item Item whose successor we are asking for
    \return The successor of specified item or head
    \sa Start, End, Retreat, NotEnd
  */
	static Type *Advance( Type *item )
	{
		return static_cast<Type *>(item->Base::next);
	}
  //! Returns previous item in the list
  /*!
    \param item Item whose predecessor we are asking for
    \return The predecessor of specified item or head
    \sa Start, End, Advance, NotEnd
  */
	static Type *Retreat( Type *item )
	{
		return static_cast<Type *>(item->Base::prev);
	}
  //! Determines wheter specified item corresponds to the head
  /*!
    \param item Item to determine
    \return Boolean value determines wheter specified item corresponds to the head
    \sa Start, End, Advance, Retreat
  */
	bool NotEnd( Type *item ) const
	{
		return item!=(Base *)this;
	}
  //! Returns first item in the list or NULL if there isn't any
  /*!
    This method and Advance and Next and Prev and Last methods provide
    safe way to iterate through the items in the list.
    \return First item in the list or NULL
    \sa Next, Prev, Last
  */
	Type *First() const
	{
		if( Base::next==const_cast<TListBidir *>(this) ) return NULL;
		Assert( Base::next );
		return static_cast<Type *>(Base::next);
	}
  //! Returns next item in the list or NULL if there isn't any
  /*!
    \param item Item whose successor we are asking for
    \return The successor of specified item or NULL
    \sa First, Prev, Last
  */
	Type *Next( const Type *item ) const
	{
		if( item->Base::next==const_cast<TListBidir *>(this) ) return NULL;
		Assert( item->Base::next );
		Assert( item->Base::next!=const_cast<Type *>(item) );
		return static_cast<Type *>(item->Base::next);
	}
  //! Returns previous item in the list or NULL if there isn't any
  /*!
    \param item Item whose predecessor we are asking for
    \return The predecessor of specified item or NULL
    \sa First, Next, Last
  */
	Type *Prev( const Type *item ) const
	{
		if( item->Base::prev==const_cast<TListBidir *>(this) ) return NULL;
		Assert( item->Base::prev );
		return static_cast<Type *>(item->Base::prev);
	}
  //! Returns last item in the list or NULL if there isn't any
  /*!
    \return Last item in the list or NULL
    \sa First, Next, Prev
  */
	Type *Last() const
	{
		if( Base::prev==const_cast<TListBidir *>(this) ) return NULL;
		Assert( Base::prev );
		return static_cast<Type *>(Base::prev);
	}
  //! Moves items from the specified source list to this list
  /*!
    Item from src are placed at the beginning of this,
    therefore src.First() becomes First() after this operation.
    Source list will be empty after this operation.
    Note that time for this operation is o(1).
    \param src Source list
  */
	void Move( TListBidir &src );
  //! Removes all items in the list from the list.
  /*!
    Note that items will not be deleted.
  */
	void Clear();
  //! Returns number of items in the list
  /*!
    Note that time for this operation is o(n).
    \param Number of items in the list
  */
	int Size() const;
  //! Determines whether there exists some item in the list.
  /*!
    \return true in case the list is empty, false elsewhere
  */
  bool Empty() const
  {
    return Base::next == const_cast<TListBidir *>(this);
  }
	bool CheckIntegrity() const;
	
	//! get totals (counter results)
	const CounterType &GetCounter() const {return _counter;}
};

template <class Type,class Base,class CounterType>
void TListBidir<Type,Base,CounterType>::Move( TListBidir &src )
{
	// take whole list src and move it into this
	// result: this contains merged list, src is empty
	// take first and last members of src
	Base *srcFirst=src.First();
	Base *srcLast=src.Last();
	if( !srcFirst ) return; // nothing to merge with
	// srcLast is not NULL
	// insert first..last before first member of this
	typename Base::PrevNextType *first=Base::next; // first member of this - can be prev (if no members)
	// link last of src with first of this
	first->Base::prev=srcLast,srcLast->Base::next=first;
	// make first of src to be first of this
	Base::next=srcFirst,srcFirst->Base::prev=this;
	src.Base::prev=src.Base::next=&src;
	// append counters
	_counter += src._counter;
	src._counter = CounterType();
}

template <class Type,class Base,class CounterType>
void TListBidir<Type,Base,CounterType>::Clear()
{
	while( First() ) Delete(First());
}

template <class Type,class Base,class CounterType>
int TListBidir<Type,Base,CounterType>::Size() const
{
	if (CounterType::IsCounted())
	{
		return _counter.GetCount();
	}
	else
	{
		// note: size may be slow - use it carefully
		int count=0;
		for( Type *obj=First(); obj; obj=Next(obj) ) count++;
		return count;
	}
}

template <class Type,class Base,class CounterType>
bool TListBidir<Type,Base,CounterType>::CheckIntegrity() const
{
	if (CounterType::IsCounted())
	{
		int count=0;
    int countedCount = _counter.GetCount();
    for(Type *i=Start(); NotEnd(i); i=Advance(i))
    {
      count++;
      if (count>2*countedCount) break; // loop is probably detected
    }
		if (count!=countedCount)
		{
			LogF("Count does not match (%d!=%d)",count,countedCount);
			return false;
		}
		
	}
	return true;
}

typedef TLinkBidir<1> TLinkBidirRef;


//! double linked list with reference counting
/*!
Any item added into the list is AddRef-ed, and it is Released when removed from the list.
Adding and removing should be never done directly by methods of TListBidir.
*/

template <class Type,class CounterType=EmptyCounter<Type> >
class TListBidirRef: public TListBidir<Type,TLinkBidirRef,CounterType>
{
	typedef TListBidir<Type,TLinkBidirRef,CounterType> base;

	public:
	void Clear();
	~TListBidirRef(){Clear();}

	void Insert( Type *element )
	{
		element->AddRef();
		base::Insert(element);
	}
	void Add( Type *element )
	{
		element->AddRef();
		base::Add(element);
	}

	void Delete( Type *element )
	{
		base::Delete(element);
		element->Release();
	}

	void InsertBefore( Type *after, Type *element )
	{
		base::InsertBefore(after,element);
		element->AddRef();
	}

	void InsertAfter( Type *before, Type *element )
	{
		base::InsertAfter(base::after,element);
		element->AddRef();
	}
	
	/// move item so that it is first item in the list
	void MakeFirst(Type *element)
	{
	  base::Delete(element);
	  base::Insert(element);
	}
	/// move item so that it is first item in the list
	void MakeLast(Type *element)
	{
	  base::Delete(element);
	  base::Add(element);
	}
};

template <class Type,class CounterType>
void TListBidirRef<Type,CounterType>::Clear()
{
  Type *item;
  while ((item=base::First())!=NULL)
  {
    Delete(item);
  }
}

#endif

