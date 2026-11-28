#ifdef _MSC_VER
#pragma once
#endif

#ifndef __POST_PROCESS_HNDL_HPP
#define __POST_PROCESS_HNDL_HPP

#include "engine.hpp"
#include <El/ParamArchive/serializeClass.hpp>

// Shared serialized handle used by engine-facing code. Backend implementations
// provide the serializer for their own post-process objects.
struct PostProcessHndl: public SerializeClass
{
  int _hndl;
  PostEffectType _type;
  Ref<RefCount> _pp;

  PostProcessHndl() {}
  PostProcessHndl(int hndl, PostEffectType type, const Ref<RefCount> &pp): _hndl(hndl), _type(type), _pp(pp) {}

  bool operator == (const PostProcessHndl &other) const
  {
    return (_hndl == other._hndl && _type == other._type && _pp == other._pp);
  }

  bool operator != (const PostProcessHndl &other) const
  {
    return !(*this==other);
  }

  LSError Serialize(ParamArchive &ar);
};

TypeIsMovable(PostProcessHndl);

static PostProcessHndl InvalidPPHndl(-1, PENone, 0);

template <>
struct FindArrayKeyTraits<PostProcessHndl>
{
  typedef const int &KeyType;
  static bool IsEqual(KeyType a, KeyType b) { return a == b; }
  static KeyType GetKey(const PostProcessHndl &a) { return a._hndl; }
};

class IPPHndlFunc
{
public:
  virtual bool operator () (PostProcessHndl &hndl) = 0;
};

#endif // __POST_PROCESS_HNDL_HPP
