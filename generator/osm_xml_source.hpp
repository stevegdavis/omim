#pragma once

#include "generator/osm_element.hpp"

class XMLSource
{
  OsmElement m_parent;
  OsmElement m_child;

  size_t m_depth = 0;
  OsmElement * m_current = nullptr;

  using TEmmiterFn = function<void(OsmElement *)>;
  TEmmiterFn m_EmmiterFn;

public:
  XMLSource(TEmmiterFn fn) : m_EmmiterFn(fn) {}

  void CharData(string const &) {}

  void AddAttr(string const & key, string const & value)
  {
    if (!m_current)
      return;

    if (key == "id")
      CHECK ( strings::to_uint64(value, m_current->id), ("Unknown element with invalid id : ", value) );
    else if (key == "lon")
      CHECK ( strings::to_double(value, m_current->lon), ("Bad node lon : ", value) );
    else if (key == "lat")
      CHECK ( strings::to_double(value, m_current->lat), ("Bad node lat : ", value) );
    else if (key == "ref")
      CHECK ( strings::to_uint64(value, m_current->ref), ("Bad node ref in way : ", value) );
    else if (key == "k")
      m_current->k = value;
    else if (key == "v")
      m_current->v = value;
    else if (key == "type")
      m_current->memberType = OsmElement::StringToEntityType(value);
    else if (key == "role")
      m_current->role = value;
  }

  bool Push(string const & tagName)
  {
    ASSERT_GREATER_OR_EQUAL(tagName.size(), 2, ());

    // As tagKey we use first two char of tag name.
    OsmElement::EntityType tagKey = OsmElement::EntityType(*reinterpret_cast<uint16_t const *>(tagName.data()));

    switch (++m_depth)
    {
      case 1:
        m_current = nullptr;
        break;
      case 2:
        m_current = &m_parent;
        m_current->type = tagKey;
        break;
      default:
        m_current = &m_child;
        m_current->type = tagKey;
    }
    return true;
  }

  void Pop(string const & v)
  {
    switch (--m_depth)
    {
      case 0:
        break;

      case 1:
        m_EmmiterFn(m_current);
        m_parent.Clear();
        break;

      default:
        switch (m_child.type)
        {
          case OsmElement::EntityType::Member:
            m_parent.AddMember(m_child.ref, m_child.memberType, m_child.role);
            break;
          case OsmElement::EntityType::Tag:
            m_parent.AddTag(m_child.k, m_child.v);
            break;
          case OsmElement::EntityType::Nd:
            m_parent.AddNd(m_child.ref);
          default:
            break;
        }
        m_current = &m_parent;
        m_child.Clear();
    }
  }
};
