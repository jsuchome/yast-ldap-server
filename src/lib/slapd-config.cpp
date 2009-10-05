/*
 * slapd-config.cpp
 *
 * A library for accessing OpenLDAP's configuration backend
 *
 * Author: Ralf Haferkamp <rhafer@suse.de>
 *
 * $Id$
 */

#include <LDAPConnection.h>
#include <LDAPResult.h>
#include <string>
#include <iostream>
#include <sstream>
#include <map>
#include <vector>
#include <LDAPEntry.h>
#include <LdifWriter.h>
#include "slapd-config.h"



#define log_it( level, string ) \
    OlcConfig::logCallback( level, string, __FILE__, __LINE__ , __FUNCTION__ )
    
static bool nocase_compare( char c1, char c2){
    return toupper(c1) == toupper(c2);
}

static int splitIndexFromString(const std::string &in, std::string &out)
{
    int index=0;
    if ( in[0] == '{' )
    {
        std::string::size_type pos = in.find('}');
        std::istringstream indexstr(in.substr(1, pos-1));
        indexstr >> index;
        out = in.substr( pos+1, std::string::npos );
    } else {
        out = in;
        index = 0;
    }
    return index;
}
static bool strCaseIgnoreEquals(const std::string &s1, const std::string &s2)
{
    if(s1.size() == s2.size()){
        if(equal(s1.begin(), s1.end(), s2.begin(),
                nocase_compare)){
            return true;
        }
    }
    return false;
}

bool OlcConfigEntry::isDatabaseEntry ( const LDAPEntry& e )
{
    StringList oc = e.getAttributeByName("objectclass")->getValues();
    for( StringList::const_iterator i = oc.begin(); i != oc.end(); i++ )
    {
        if ( strCaseIgnoreEquals(*i, "olcDatabaseConfig" ) )
        {
            return true;
        }
    }
    return false;
}

bool OlcConfigEntry::isGlobalEntry ( const LDAPEntry& e )
{
    StringList oc = e.getAttributeByName("objectclass")->getValues();
    for( StringList::const_iterator i = oc.begin(); i != oc.end(); i++ )
    {
        if ( strCaseIgnoreEquals(*i, "olcGlobal" ) )
        {
            return true;
        }
    }
    return false;
}

bool OlcConfigEntry::isOverlayEntry ( const LDAPEntry& e )
{
    StringList oc = e.getAttributeByName("objectclass")->getValues();
    for( StringList::const_iterator i = oc.begin(); i != oc.end(); i++ )
    {
        if ( strCaseIgnoreEquals(*i, "olcOverlayConfig" ) )
        {
            return true;
        }
    }
    return false;
}

bool OlcConfigEntry::isScheamEntry ( const LDAPEntry& e )
{
    StringList oc = e.getAttributeByName("objectclass")->getValues();
    for( StringList::const_iterator i = oc.begin(); i != oc.end(); i++ )
    {
        if ( strCaseIgnoreEquals(*i, "olcSchemaConfig" ) )
        {
            return true;
        }
    }
    return false;
}

OlcConfigEntry* OlcConfigEntry::createFromLdapEntry( const LDAPEntry& e )
{
    if ( OlcConfigEntry::isGlobalEntry(e) )
    {
        log_it(SLAPD_LOG_INFO,"creating OlcGlobalConfig" );
        return new OlcGlobalConfig(e);
    }
    else if ( OlcConfigEntry::isScheamEntry(e) )
    {
        log_it(SLAPD_LOG_INFO,"creating OlcSchemaConfig" );
        return new OlcSchemaConfig(e);
    }
    else if ( OlcConfigEntry::isDatabaseEntry(e) )
    {
        log_it(SLAPD_LOG_INFO,"creating OlcDatabase" );;
        return OlcDatabase::createFromLdapEntry(e);
    }
    else if ( OlcConfigEntry::isOverlayEntry(e) )
    {
        log_it(SLAPD_LOG_INFO,"creating OlcOverlay");
        return new OlcConfigEntry(e);
    }
    else
    {
        log_it(SLAPD_LOG_INFO,"unknown Config Object" );
        return 0;
    }
}

void OlcConfigEntry::setIndex( int index, bool origEntry )
{
    this->entryIndex = index;
    this->updateEntryDn( origEntry );
}

int OlcConfigEntry::getEntryIndex() const
{
    return this->entryIndex;
}

void OlcConfigEntry::updateEntryDn( bool origEntry )
{
}

void OlcConfigEntry::clearChangedEntry()
{
   m_dbEntryChanged = LDAPEntry();     
}

void OlcConfigEntry::resetEntries( const LDAPEntry &e )
{
    m_dbEntry = e;
    m_dbEntryChanged = e;
    this->resetMemberAttrs();
}

StringList OlcConfigEntry::getStringValues(const std::string &type) const
{
    const LDAPAttribute *attr = m_dbEntryChanged.getAttributeByName(type);
    if ( attr ) {
        return attr->getValues();
    } else {
        return StringList();
    }
}

std::string OlcConfigEntry::getStringValue(const std::string &type) const
{
    StringList sl = this->getStringValues(type);
    if ( sl.size() == 1 ) {
        return *(sl.begin());
    } else {
        return "";
    }
}

void OlcConfigEntry::setStringValues(const std::string &type, const StringList &values)
{
    LDAPAttribute attr(type, values);
    m_dbEntryChanged.replaceAttribute(attr);
}

void OlcConfigEntry::setStringValue(const std::string &type, const std::string &value)
{
    log_it(SLAPD_LOG_INFO,"setStringValue() " + type + " " + value);
    if ( value.empty() )
    {
        m_dbEntryChanged.delAttribute(type);
    }
    else
    {
        LDAPAttribute attr(type, value);
        m_dbEntryChanged.replaceAttribute(attr);
    }
}

void OlcConfigEntry::addStringValue(const std::string &type, const std::string &value)
{
    const LDAPAttribute *attr =  m_dbEntryChanged.getAttributeByName(type);
    if ( attr ) {
        LDAPAttribute newAttr(*attr);
        newAttr.addValue(value);
        m_dbEntryChanged.replaceAttribute(newAttr);
    } else {
        LDAPAttribute newAttr(type, value);
        m_dbEntryChanged.addAttribute(newAttr);
    }
}

void OlcConfigEntry::addIndexedStringValue(const std::string &type,
        const std::string &value, int index)
{
    std::ostringstream oStr;
    oStr << "{" << index << "}" << value;
    this->addStringValue( type, oStr.str() );
}

int OlcConfigEntry::getIntValue( const std::string &type ) const
{
    StringList sl = this->getStringValues(type);
    if ( sl.empty() )
    {
        return -1;
    }
    else if(sl.size() == 1 ) {
        std::istringstream iStr(*sl.begin());
        int value;
        iStr >> value;
        return value;
    } else {
        throw(std::runtime_error("Attribute is not single-valued") );
    }
}

void OlcConfigEntry::setIntValue( const std::string &type, int value )
{
    std::ostringstream oStr;
    oStr << value;
    this->setStringValue( type, oStr.str() );
}

std::string OlcConfigEntry::toLdif() const
{
    std::ostringstream ldifStream;
    LdifWriter ldif(ldifStream);
    ldif.writeRecord( m_dbEntryChanged );
    return ldifStream.str();
}

bool OlcConfigEntry::isNewEntry() const
{
    return ( this->getDn().empty() );
}
bool OlcConfigEntry::isDeletedEntry() const
{
    return ( (!this->getDn().empty()) && this->getUpdatedDn().empty() );
}

LDAPModList OlcConfigEntry::entryDifftoMod() const {
    LDAPAttributeList::const_iterator i = m_dbEntry.getAttributes()->begin();
    LDAPModList modifications;
    log_it(SLAPD_LOG_INFO, "Old Entry DN: " + m_dbEntry.getDN());
    log_it(SLAPD_LOG_INFO,"New Entry DN: " + m_dbEntryChanged.getDN());
    for(; i != m_dbEntry.getAttributes()->end(); i++ )
    {
        log_it(SLAPD_LOG_INFO,i->getName());
        const LDAPAttribute *changedAttr =  m_dbEntryChanged.getAttributeByName(i->getName());
        if ( changedAttr ) {
            StringList::const_iterator j = i->getValues().begin();
            StringList delValues, addValues;
            for(; j != i->getValues().end(); j++ )
            {
                bool deleted = true;
                StringList::const_iterator k = changedAttr->getValues().begin();
                for( ; k != changedAttr->getValues().end(); k++ ) {
                    if ( *k == *j ) {
                        deleted = false;
                        break;
                    }
                }
                if ( deleted ) 
                {
                    delValues.add(*j);
                    log_it(SLAPD_LOG_INFO,"Value deleted: " + *j );
                }
            }
            j = changedAttr->getValues().begin();
            for(; j != changedAttr->getValues().end(); j++ )
            {
                bool added = true;
                StringList::const_iterator k = i->getValues().begin();
                for( ; k != i->getValues().end(); k++ ) {
                    if ( *k == *j ) {
                        log_it(SLAPD_LOG_DEBUG,"Value unchanged: " + *k );
                        added = false;
                        break;
                    }
                }
                if ( added ) 
                {
                    addValues.add(*j);
                    log_it(SLAPD_LOG_INFO,"Value added: " + *j);
                }
            }
            bool replace = false;
            if ( delValues.size() > 0 ) {
                if ( (addValues.size() > 0) && ( (int)delValues.size() == i->getNumValues()) ) {
                    log_it(SLAPD_LOG_INFO,"All Values deleted, this is a replace" );
                    modifications.addModification(
                            LDAPModification( LDAPAttribute(i->getName(), addValues), 
                                    LDAPModification::OP_REPLACE) 
                            );
                    replace = true;
                } else {
                    modifications.addModification(
                            LDAPModification( LDAPAttribute(i->getName(), delValues ), 
                                    LDAPModification::OP_DELETE) 
                            );
                }
            }
            if (addValues.size() > 0 && !replace ) {
                modifications.addModification(
                        LDAPModification( LDAPAttribute(i->getName(), addValues), 
                                LDAPModification::OP_ADD) 
                        );
            }
        } else {
            log_it(SLAPD_LOG_INFO,"removed Attribute: " + i->getName() );
            modifications.addModification(
                    LDAPModification( LDAPAttribute(i->getName()), 
                            LDAPModification::OP_DELETE)
                    );
        }
    }
    i = m_dbEntryChanged.getAttributes()->begin();
    for(; i != m_dbEntryChanged.getAttributes()->end(); i++ )
    {
        log_it(SLAPD_LOG_DEBUG,i->getName() );
        const LDAPAttribute *old =  m_dbEntry.getAttributeByName(i->getName());
        if (! old ) {
            log_it(SLAPD_LOG_INFO,"Attribute added: " + i->getName());
            if (! i->getValues().empty() )
            {
                modifications.addModification(
                        LDAPModification( LDAPAttribute(i->getName(), i->getValues()), 
                                    LDAPModification::OP_ADD) 
                        );
            }
        }
    }
    return modifications;
}

OlcOverlay* OlcOverlay::createFromLdapEntry( const LDAPEntry& e)
{
    return new OlcOverlay(e);
}

OlcOverlay::OlcOverlay( const LDAPEntry& e) : OlcConfigEntry(e)
{
    log_it(SLAPD_LOG_INFO,"OlcOverlay::OlcOverlay()" );
    std::string type(this->getStringValue("olcoverlay"));
    entryIndex = splitIndexFromString( type, m_type );
}

OlcOverlay::OlcOverlay( const std::string &type, const std::string &parent )
        : m_type(type), m_parent(parent)
{
    std::ostringstream dnstr;
    dnstr << "olcOverlay=" << m_type << "," << parent;
    m_dbEntryChanged.setDN(dnstr.str());
    m_dbEntryChanged.addAttribute(LDAPAttribute("objectclass", "olcPpolicyConfig"));
    m_dbEntryChanged.addAttribute(LDAPAttribute("olcoverlay", m_type));
}

const std::string OlcOverlay::getType() const
{
    return m_type;
}

void OlcOverlay::newParentDn( const std::string& parent )
{
    std::ostringstream dnstr;
    m_parent = parent;
    dnstr << "olcOverlay={" << entryIndex << "}" << m_type << "," << parent;
    log_it(SLAPD_LOG_INFO, "Changing Overlay DN from: " + m_dbEntryChanged.getDN()
                           + " to: " + dnstr.str() );
    if (! m_dbEntry.getDN().empty() )
    {
        m_dbEntry.setDN(dnstr.str());
    }
    m_dbEntryChanged.setDN(dnstr.str());
}

void OlcOverlay::resetMemberAttrs()
{
    std::string type(this->getStringValue("olcoverlay"));
    entryIndex = splitIndexFromString( type, m_type );
}
void OlcOverlay::updateEntryDn(bool origEntry )
{
    log_it(SLAPD_LOG_INFO, "updateEntryDN()");
    std::ostringstream dn, name;
    name << "{" << entryIndex << "}" << m_type;
    dn << "olcOverlay=" << name.str() << "," << m_parent;
    m_dbEntryChanged.setDN(dn.str());
    m_dbEntryChanged.replaceAttribute(LDAPAttribute("olcOverlay", name.str()));
    if ( origEntry && (! m_dbEntry.getDN().empty()) )
    {
        m_dbEntry.setDN(dn.str());
        m_dbEntry.replaceAttribute(LDAPAttribute("olcOverlay", name.str()));
    }
}

static int extractAlcToken( const std::string& acl, std::string::size_type& startpos, bool quoted )
{
    std::string::size_type pos;

    // skip leading whitespaces
    startpos = acl.find_first_not_of("\t ", startpos );

    if ( quoted && acl[startpos] == '"' )
    {
        // find matching (unescapted) quote 
        startpos++;
        pos = startpos;
        bool found=false;
        while( ! found )
        {
            pos = acl.find_first_of('"', pos );
            if ( pos == std::string::npos )
            {
                break;
            }
            if ( acl[pos-1] != '\\' )
            {
                found = true;
            }
        }
        if ( !found )
        {
            log_it(SLAPD_LOG_ERR, "Not matching quote found" );
        }
    }
    else
    {
        pos = acl.find_first_of("\t ", startpos );
    }
    return pos;
}

OlcAccess::OlcAccess( const std::string& aclString )
{
    std::string::size_type spos = 0;
    std::string::size_type tmppos = 0;
    // every ACL starts with "to"
    if ( aclString.compare(0, 2, "to") != 0 )
    {
        log_it(SLAPD_LOG_ERR, "acl does not start with \"to\"" );
        throw std::runtime_error( "acl does not start with \"to\"" );
    }
    spos+=2;

    // skip whitespaces
    tmppos = aclString.find_first_not_of("\t ", spos );
    if ( tmppos != std::string::npos && tmppos > spos )
    {
        spos = tmppos;
    }

    // we should be at the start of the "what" part now, might `*` 
    // or a string followed by '='
    if ( aclString[spos] == '*' )
    {
        log_it(SLAPD_LOG_ERR, "acl matches all entries" );
        spos = tmppos+1;
        tmppos = extractAlcToken( aclString, spos, true );
        m_all = true;
    }
    else
    {
        m_all = false;
        while ( true )
        {
            tmppos = aclString.find_first_of("=\t ", spos );
            if ( tmppos == std::string::npos )
            {
                log_it(SLAPD_LOG_ERR, "Unexpected end of ACL" );
                throw std::runtime_error( "Unexpected end of ACL" );
            }
            else
            {
                std::string whatType = aclString.substr(spos, tmppos-spos);
                log_it(SLAPD_LOG_INFO, "Whattype: " +  whatType );
                if ( aclString.substr(spos, tmppos-spos) == "by" )
                {
                    break;
                }
                spos = tmppos+1;

                tmppos = extractAlcToken( aclString, spos, true );
                
                log_it(SLAPD_LOG_INFO, "Whatvalue: " +  aclString.substr(spos, tmppos-spos) );
                if ( whatType == "filter" )
                {
                    m_filter = aclString.substr(spos, tmppos-spos);
                }
                else if (whatType == "attrs")
                {
                    m_attributes = aclString.substr(spos, tmppos-spos);
                }
                else if (whatType == "dn.base" || whatType == "dn.subtree" )
                {
                    m_dn_type = whatType;
                    m_dn_value = aclString.substr(spos, tmppos-spos);
                }
                else
                {
                    throw std::runtime_error( "Can't parse ACL unsupported \"what\": \"" + whatType + "\"" );
                }
                spos = aclString.find_first_not_of("\t ", tmppos+1 );
            }
        }
    }
    // we should have reached the "by"-clauses now
    while ( true )
    {
        if ( spos == std::string::npos )
        {
            throw std::runtime_error( "Error while parsing ACL by clause" );
        }
        if ( aclString.substr(spos, tmppos-spos) != "by" )
        {
            if (tmppos != std::string::npos )
            {
                throw std::runtime_error( "Error while parsing ACL by clause" );
            }
            break;
        }
        else
        {
            std::string type = "";
            std::string value = "";
            std::string level = "";
            std::string control = "";
            spos = tmppos+1;
            // skip whitespaces
            tmppos = aclString.find_first_not_of("\t ", spos );
            if ( tmppos != std::string::npos && tmppos > spos )
            {
                spos = tmppos;
            }

            // we should be at the start of the "by" part now, might `*` 
            // or a string followed by '='
            if ( aclString[spos] == '*' )
            {
                log_it(SLAPD_LOG_ERR, "by clause matches all entries" );
                type = "*";
            }
            tmppos = aclString.find_first_of("=\t ", spos );
            if ( tmppos == std::string::npos )
            {
                log_it(SLAPD_LOG_ERR, "Unexpected end of ACL" );
                throw std::runtime_error( "Error while parsing ACL" );
            }
            else
            {
                if ( type.empty() )
                    type = aclString.substr(spos, tmppos-spos);

                log_it(SLAPD_LOG_INFO, "bytype: " +  type );
                if ( type == "group" || type == "dn.base" || type == "dn.subtree" )
                {
                    if ( aclString[tmppos] == '=' )
                    {
                        spos = tmppos+1;
                        tmppos = extractAlcToken( aclString, spos, true );
                        value = aclString.substr(spos, tmppos-spos);
                        log_it(SLAPD_LOG_INFO, "byvalue: " +  value );
                    }
                    else
                    {
                        throw std::runtime_error( "Error while parsing ACL, expected \"=\"" );
                    }
                }
                else if ( type != "users" && type != "anonymous" && type != "self" && type != "*" )
                {
                    throw std::runtime_error( "Unsupported \"by\" clause" );
                }
                spos = tmppos+1;
                tmppos = extractAlcToken( aclString, spos, false );
                level = aclString.substr(spos, tmppos-spos);
                if ( !level.empty() && 
                     level != "none" && level != "disclose" && level != "auth" &&
                     level != "compare" && level != "read" &&
                     level != "write" && level != "manage" )
                {
                    if ( level == "stop" || level == "break" || level == "continue" )
                    {
                        // it's ok to have no access level defined
                        control = level;
                        level = "";
                    }
                    else
                    {
                        throw std::runtime_error( "Unsupported access level" );
                    }
                }
                log_it(SLAPD_LOG_INFO, "access: " +  level );
                if ( control.empty() && tmppos != std::string::npos )
                {
                    spos = tmppos+1;
                    tmppos = extractAlcToken( aclString, spos, false );
                    control = aclString.substr(spos, tmppos-spos);
                    log_it(SLAPD_LOG_INFO, "control: " +  control );
                    if ( control != "stop" && control != "break" && control != "continue" )
                    {
                        control = "";
                        tmppos = spos-1;
                    }
                }
                if (tmppos != std::string::npos )
                {
                    spos = aclString.find_first_not_of("\t ", tmppos+1 );
                    if ( spos != std::string::npos )
                    {
                        tmppos = aclString.find_first_of("\t ", spos );
                    }
                    else
                    {
                        tmppos = spos;
                    }
                }
            }
            log_it(SLAPD_LOG_INFO, "level <"+level+"> type <"+type+"> value <"+value+"> control <" + control + ">" );
            boost::shared_ptr<OlcAclBy> by( new OlcAclBy(level, type, value, control) );
            m_byList.push_back(by);
        }
    }
}

void OlcAccess::setFilter( const std::string& filter )
{
    m_filter = filter;
}


void OlcAccess::setAttributes( const std::string& attrs )
{
    m_attributes = attrs;
}

void OlcAccess::setDnType( const std::string& dnType )
{
    if ( dnType == "dn.base" || dnType == "dn.subtree" )
    {
        m_dn_type = dnType;
    }
}

void OlcAccess::setDn( const std::string& dn )
{
    m_dn_value = dn;
}

void OlcAccess::setMatchAll( bool matchAll )
{
    m_all = matchAll;
    if ( matchAll )
    {
        m_attributes = "";
        m_dn_type = "";
        m_dn_value = "";
        m_filter = "";
    }
}

void OlcAccess::setByList( const OlcAclByList& byList )
{
    m_byList = byList;
}

std::string OlcAccess::getFilter() const
{
    return m_filter;
}

std::string OlcAccess::getAttributes() const
{
    return m_attributes;
}

std::string OlcAccess::getDnType() const
{
    return m_dn_type;
}

std::string OlcAccess::getDnValue() const
{
    return m_dn_value;
}

bool OlcAccess::matchesAll() const
{
    return m_all;
}

OlcAclByList OlcAccess::getAclByList() const
{
    return m_byList;
}

std::string OlcAccess::toAclString() const
{
    std::ostringstream aclString;

    aclString << "to";
    if ( m_all )
    {
        aclString << " *";
    }
    else
    {
        if ( ! m_dn_type.empty() )
        {
            aclString << " " << m_dn_type << "=\"" << m_dn_value << "\"";
        }
        if ( ! m_filter.empty() )
        {
            aclString << " filter=" << m_filter;
        }
        if ( ! m_attributes.empty() )
        {
            aclString << " attrs=" << m_attributes;
        }
    }
    
    if ( m_byList.empty() )
    {
        throw(std::runtime_error("ACL byList is empty"));
    }

    OlcAclByList::const_iterator i;
    for ( i = m_byList.begin(); i != m_byList.end(); i++ )
    {
        aclString << " by " << (*i)->getType();
        if ( (*i)->getType() == "dn.base" ||
             (*i)->getType() == "dn.subtree" ||
             (*i)->getType() == "group" )
        {
            aclString << "=\"" << (*i)->getValue() << "\"";
        }
        if ( ! (*i)->getLevel().empty() )
        {
            aclString << " " << (*i)->getLevel();
        }
        std::string control = (*i)->getControl();
        if ( !control.empty() && control != "stop" )
        {
            aclString << " " << control;
        }
    }
    return aclString.str();
}


OlcDatabase::OlcDatabase( const LDAPEntry& le=LDAPEntry()) : OlcConfigEntry(le)
{
    std::string type(this->getStringValue("olcdatabase"));
    entryIndex = splitIndexFromString( type, m_type );
}

OlcDatabase::OlcDatabase( const std::string& type ) : m_type(type) 
{
    std::ostringstream dnstr;
    dnstr << "olcDatabase=" << m_type << ",cn=config";
    m_dbEntryChanged.setDN(dnstr.str());
    m_dbEntryChanged.addAttribute(LDAPAttribute("objectclass", "olcDatabaseConfig"));
    m_dbEntryChanged.addAttribute(LDAPAttribute("olcDatabase", m_type));
}

void OlcDatabase::updateEntryDn(bool origEntry )
{
    log_it(SLAPD_LOG_INFO, "updateEntryDN()");
    std::ostringstream dn, name;
    name << "{" << entryIndex << "}" << m_type;
    dn << "olcDatabase=" << name.str() << ",cn=config" ;
    m_dbEntryChanged.setDN(dn.str());
    m_dbEntryChanged.replaceAttribute(LDAPAttribute("olcDatabase", name.str()));
    if ( origEntry && (! m_dbEntry.getDN().empty()) )
    {
        m_dbEntry.setDN(dn.str());
        m_dbEntry.replaceAttribute(LDAPAttribute("olcDatabase", name.str()));
    }
}

void OlcDatabase::setSuffix( const std::string &suffix)
{
    this->setStringValue("olcSuffix", suffix); 
}

void OlcDatabase::setRootDn( const std::string &rootdn)
{
    this->setStringValue("olcRootDN", rootdn); 
}

void OlcDatabase::setRootPw( const std::string &rootpw)
{
    this->setStringValue("olcRootPW", rootpw); 
}

const std::string OlcDatabase::getSuffix() const
{
    return this->getStringValue("olcSuffix");
}

const std::string OlcDatabase::getType() const
{
    return this->m_type;
}

bool OlcDatabase::getAcl(OlcAccessList &aclList) const
{
    const LDAPAttribute* aclAttr = m_dbEntryChanged.getAttributeByName("olcAccess");
    aclList.clear();
    bool ret = true;
    if ( aclAttr )
    {
        StringList values = aclAttr->getValues();
        StringList::const_iterator i;
        for ( i =  values.begin(); i != values.end(); i++ )
        {
            log_it(SLAPD_LOG_INFO, "acl VALUE: " + *i );
            std::string aclString;
            splitIndexFromString( *i, aclString );
            try {
                boost::shared_ptr<OlcAccess> acl( new OlcAccess(aclString) );
                aclList.push_back(acl);
            }
            catch ( std::runtime_error e )
            {
                log_it(SLAPD_LOG_INFO, "Can't parse ACL");
                log_it(SLAPD_LOG_INFO, e.what() );
                aclList.clear();
                ret = false;
                break;
            }
        }
    }
    return ret;
}

void OlcDatabase::addAccessControl(const std::string& acl, int index )
{
    if ( index < 0 )
    {
        StringList sl = this->getStringValues( "olcAccess" );
        index = sl.size();
    }
    this->addIndexedStringValue( "olcAccess", acl, index );
}

void OlcDatabase::replaceAccessControl(const OlcAccessList& acllist )
{
    // delete old Values first
    this->setStringValue( "olcAccess", "" );

    OlcAccessList::const_iterator i;
    int j = 0;

    for ( i = acllist.begin(); i != acllist.end(); i++ )
    {
        this->addAccessControl( (*i)->toAclString(), j );
        j++;
    }
}

void OlcDatabase::addOverlay(boost::shared_ptr<OlcOverlay> overlay)
{
    m_overlays.push_back(overlay);
}

OlcOverlayList& OlcDatabase::getOverlays()
{
    return m_overlays;
}

void OlcDatabase::resetMemberAttrs()
{
    std::string type(this->getStringValue("olcdatabase"));
    entryIndex = splitIndexFromString( type, m_type );
}

bool OlcDatabase::isBdbDatabase( const LDAPEntry& e )
{
    StringList oc = e.getAttributeByName("objectclass")->getValues();
    for( StringList::const_iterator i = oc.begin(); i != oc.end(); i++ )
    {
        if ( strCaseIgnoreEquals(*i, "olcBdbConfig" ) || strCaseIgnoreEquals(*i, "olcHdbConfig" ) )
        {
            return true;
        }
    }
    return false;
}

OlcDatabase* OlcDatabase::createFromLdapEntry( const LDAPEntry& e)
{
    if ( OlcDatabase::isBdbDatabase( e ) )
    {
        log_it(SLAPD_LOG_INFO,"creating OlcBbdDatabase()" );
        return new OlcBdbDatabase(e);
    }
    else
    {
        log_it(SLAPD_LOG_INFO,"creating OlcDatabase()" );
        return new OlcDatabase(e);
    }
}


OlcBdbDatabase::OlcBdbDatabase( const std::string& type ) : OlcDatabase(type) 
{ 
    if ( type == "hdb" )
    {
        m_dbEntryChanged.addAttribute(LDAPAttribute("objectclass", "olcHdbConfig"));
    }
    else
    {
        m_dbEntryChanged.addAttribute(LDAPAttribute("objectclass", "olcBdbConfig"));
    }
}

OlcBdbDatabase::OlcBdbDatabase( const LDAPEntry& le) : OlcDatabase(le) { }

inline void splitIndexString( const std::string &indexString, std::string &attr, std::string &indexes )
{
    std::string::size_type pos = indexString.find_first_of(" \t");
    attr = indexString.substr(0, pos);
    log_it(SLAPD_LOG_DEBUG, "AttributeType: " + attr );
    if ( pos != std::string::npos ) {
        pos = indexString.find_first_not_of(" \t", pos);
        if ( pos != std::string::npos ) {
            indexes = indexString.substr( pos, std::string::npos );
        }
    }
}

inline std::vector<IndexType> indexString2Type( const std::string &indexes )
{
    std::string::size_type pos, oldpos = 0;
    std::vector<IndexType> idx;
    do {
        pos = indexes.find( ',', oldpos );
        std::string index = indexes.substr( oldpos, 
                    (pos == std::string::npos ? std::string::npos : pos - oldpos) );
        log_it(SLAPD_LOG_INFO, std::string("Index: ") + index );
        oldpos = indexes.find_first_not_of( ", ", pos );
        if ( index == "pres" ) {
            idx.push_back(Present);
        } else if (index == "eq" ) {
            idx.push_back(Eq);
        } else if (index == "approx" ) {
            idx.push_back(Approx);
        } else if (index == "sub" ) {
            idx.push_back(Sub);
        } else if (index == "subinital" ) {
            idx.push_back(SpecialSubInitial);
        } else if (index == "subany" ) {
            idx.push_back(SpecialSubAny);
        } else if (index == "subfinal" ) {
            idx.push_back(SpecialSubFinal);
        } else if (index == "nolang" ) {
            idx.push_back(SpecialNoLang);
        } else if (index == "nosubtypes" ) {
            idx.push_back(SpecialNoSubTypes);
        }
    } while (pos != std::string::npos);
    return idx;
}

IndexMap OlcBdbDatabase::getDatabaseIndexes() const
{
    const LDAPAttributeList *al = m_dbEntryChanged.getAttributes();
    const LDAPAttribute *attr = al->getAttributeByName("olcdbindex");
    IndexMap res;
    if (! attr ) {
        return res;
    };

    StringList sl = attr->getValues();
    StringList::const_iterator i;
    for (i = sl.begin(); i != sl.end(); i++ ) {
        std::string attrType;
        std::string indexes;
        splitIndexString(*i, attrType, indexes );
        log_it(SLAPD_LOG_INFO, "Indexes: " + indexes );
        std::vector<IndexType> idx = indexString2Type(indexes);
        res.insert(make_pair(attrType, idx));
    }
    return res;
}

std::vector<IndexType> OlcBdbDatabase::getDatabaseIndex( const std::string &type ) const
{
    const LDAPAttributeList *al = m_dbEntryChanged.getAttributes();
    const LDAPAttribute *attr = al->getAttributeByName("olcdbindex");
    std::vector<IndexType> res;
    if (! attr ) {
        return res;
    };

    StringList sl = attr->getValues();
    StringList::const_iterator i;
    for (i = sl.begin(); i != sl.end(); i++ ) {
        std::string attrType;
        std::string indexes;
        splitIndexString(*i, attrType, indexes );
        if ( attrType == type )
        {
            res = indexString2Type(indexes);
            break;
        }
    }
    return res;
}

void OlcBdbDatabase::addIndex(const std::string& attr, const std::vector<IndexType>& idx)
{
    std::string indexString = attr;
    std::vector<IndexType>::const_iterator i;
    bool first = true;
    for ( i = idx.begin(); i != idx.end(); i++ )
    {
        if (! first)
        {
            indexString += ",";
        } else {
            indexString += " ";
            first = false;
        }
        if ( *i == Present ) {
            indexString += "pres";
        }
        else if ( *i == Eq )
        {
            indexString += "eq";
        }
        else if ( *i == Sub )
        {
            indexString += "sub";
        }
    }
    log_it(SLAPD_LOG_INFO, "indexString: '" + indexString + "'");
    this->addStringValue( "olcDbIndex", indexString );
}

void OlcBdbDatabase::deleteIndex(const std::string& type)
{
    const LDAPAttribute *attr = m_dbEntryChanged.getAttributes()->getAttributeByName("olcdbindex");
    if (! attr ) {
        return;
    };
    
    StringList sl = attr->getValues();
    StringList newValues;
    StringList::const_iterator i;
    for (i = sl.begin(); i != sl.end(); i++ ) {
        std::string attrType;
        std::string indexes;
        splitIndexString(*i, attrType, indexes );
        if ( attrType != type )
        {
            newValues.add(*i);
        }
    }
    this->setStringValues("olcdbindex", newValues );
}

void OlcBdbDatabase::setDirectory( const std::string &dir )
{   
    this->setStringValue("olcDbDirectory", dir);
}

int OlcBdbDatabase::getEntryCache() const
{
    return this->getIntValue( "olcDbCachesize" );
}

void OlcBdbDatabase::setEntryCache( int cachesize )
{
    if (! cachesize )
    {
        this->setStringValue( "olcDbCachesize", "" );
    }
    else
    {
        this->setIntValue( "olcDbCachesize", cachesize );
    }
}

int OlcBdbDatabase::getIdlCache() const
{
    return this->getIntValue( "olcDbIdlCachesize" );
}

void OlcBdbDatabase::setIdlCache( int cachesize )
{
    if (! cachesize )
    {
        this->setStringValue( "olcDbIdlCachesize", "" );
    }
    else
    {
        this->setIntValue( "olcDbIdlCachesize", cachesize );
    }
}

void OlcBdbDatabase::setCheckPoint( int kbytes, int min )
{
    if ( !kbytes && !min )
    {
        this->setStringValue( "olcDbCheckpoint", "" );
    }
    else
    {
        std::ostringstream oStr;
        oStr << kbytes << " " << min;
        this->setStringValue( "olcDbCheckpoint", oStr.str() );
    }
}

void OlcBdbDatabase::getCheckPoint( int& kbytes, int& min) const
{
    kbytes=0;
    min=0;
    std::string checkpointStr =  this->getStringValue("olcDbCheckpoint");
    if (! checkpointStr.empty() )
    {
        std::istringstream iStr(checkpointStr);
        iStr >> kbytes >> std::skipws >> min;
    }
    return;
}

OlcGlobalConfig::OlcGlobalConfig() : OlcConfigEntry()
{
    m_dbEntryChanged.setDN("cn=config");
    m_dbEntryChanged.addAttribute(LDAPAttribute("objectclass", "olcGlobal"));
    m_dbEntryChanged.addAttribute(LDAPAttribute("cn", "config"));
}

OlcGlobalConfig::OlcGlobalConfig( const LDAPEntry &le) : OlcConfigEntry(le)
{
    log_it(SLAPD_LOG_INFO, "OlcGlobalConfig::OlcGlobalConfig( const LDAPEntry &le) : OlcConfigEntry(le)");

}

const std::vector<std::string> OlcGlobalConfig::getLogLevelString() const
{
    StringList lvalues = this->getStringValues("olcLogLevel");
    StringList::const_iterator i;
    std::vector<std::string> lvls;
    for ( i = lvalues.begin(); i != lvalues.end(); i++ )
    {
        std::istringstream iss(*i);
        int intlogValue;
        if ( iss >> intlogValue ) {
            log_it(SLAPD_LOG_INFO,"IntegerValue " + *i );
        }
        else
        {
            log_it(SLAPD_LOG_INFO,"StringValue " + *i );
            lvls.push_back(*i);
        }
    }
    return lvls;
}

void OlcGlobalConfig::setLogLevel(const std::list<std::string> &level) {
    const LDAPAttribute *sattr = m_dbEntryChanged.getAttributeByName("olcloglevel");
    LDAPAttribute attr( "olcloglevel" );
    if ( sattr ) {
        attr = *sattr;
    }
    StringList values;
    std::list<std::string>::const_iterator i = level.begin();
    for(; i != level.end(); i++ )
    {
        values.add(*i);
    }
    attr.setValues(values);
    m_dbEntryChanged.replaceAttribute(attr);
}

void OlcGlobalConfig::addLogLevel(std::string level) {
    const LDAPAttribute *sattr = m_dbEntryChanged.getAttributeByName("olcloglevel");
    LDAPAttribute attr;
    if ( sattr ) {
        attr = *sattr;
    }
    attr.addValue(level);
    m_dbEntryChanged.replaceAttribute(attr);
}

const std::vector<std::string> OlcGlobalConfig::getAllowFeatures() const
{
    StringList values = this->getStringValues("olcAllows");
    StringList::const_iterator i;
    std::vector<std::string> allow;
    for ( i = values.begin(); i != values.end(); i++ )
    {
        allow.push_back(*i);
    }
    return allow;
}

void OlcGlobalConfig::setAllowFeatures(const std::list<std::string> &allow )
{
    const LDAPAttribute *sattr = m_dbEntryChanged.getAttributeByName("olcAllows");
    LDAPAttribute attr( "olcAllows" );
    if ( sattr ) {
        attr = *sattr;
    }
    StringList values;
    std::list<std::string>::const_iterator i = allow.begin();
    for(; i != allow.end(); i++ )
    {
        values.add(*i);
    }
    attr.setValues(values);
    m_dbEntryChanged.replaceAttribute(attr);
}

const std::vector<std::string> OlcGlobalConfig::getDisallowFeatures() const
{
    StringList values = this->getStringValues("olcDisallows");
    StringList::const_iterator i;
    std::vector<std::string> allow;
    for ( i = values.begin(); i != values.end(); i++ )
    {
        allow.push_back(*i);
    }
    return allow;
}

void OlcGlobalConfig::setDisallowFeatures(const std::list<std::string> &disallow )
{
    const LDAPAttribute *sattr = m_dbEntryChanged.getAttributeByName("olcDisallows");
    LDAPAttribute attr( "olcDisallows" );
    if ( sattr ) {
        attr = *sattr;
    }
    StringList values;
    std::list<std::string>::const_iterator i = disallow.begin();
    for(; i != disallow.end(); i++ )
    {
        values.add(*i);
    }
    attr.setValues(values);
    m_dbEntryChanged.replaceAttribute(attr);
}



OlcTlsSettings OlcGlobalConfig::getTlsSettings() const 
{
    log_it(SLAPD_LOG_INFO, "OlcTlsSettings OlcGlobalConfig::getTlsSettings() const ");
    return OlcTlsSettings( *this );
}

void OlcGlobalConfig::setTlsSettings( const OlcTlsSettings& tls )
{
    tls.applySettings( *this );
}

const std::string OlcSchemaConfig::schemabase = "cn=schema,cn=config";

OlcSchemaConfig::OlcSchemaConfig() : OlcConfigEntry()
{
    m_dbEntryChanged.setDN("cn=schema,cn=config");
    m_dbEntryChanged.addAttribute(LDAPAttribute("objectclass", "olcSchemaConfig"));
    m_dbEntryChanged.addAttribute(LDAPAttribute("cn", "schema"));
}

OlcSchemaConfig::OlcSchemaConfig(const LDAPEntry &e) : OlcConfigEntry(e)
{
    log_it(SLAPD_LOG_INFO, "OlcSchemaConfig::OlcSchemaConfig(const LDAPEntry &e) : OlcConfigEntry(e)");
    std::string name(this->getStringValue("cn"));
    std::string dn(e.getDN() );
    if ( name.empty() )
    {
        throw std::runtime_error( "Entry '" + dn + "' has no 'cn' Attribute." );
    }
    if ( (dn.size() < schemabase.size()) || 
         (dn.compare( dn.size()-schemabase.size(), schemabase.size(), schemabase ))  )
    {
        throw std::runtime_error( "Entry '" + dn + "' is not a child of '" + schemabase + "'." );
    }
    if ( this->getStringValues("olcobjectclasses").empty() &&
         this->getStringValues("olcattributetypes").empty() )
    {
        throw std::runtime_error( "Entry '" + dn + "' does not define any objectclasses or attributetypes." );
    }

    entryIndex = splitIndexFromString( name, m_name );
}
OlcSchemaConfig::OlcSchemaConfig(const LDAPEntry &e1, const LDAPEntry &e2) : OlcConfigEntry(e1, e2)
{
    log_it(SLAPD_LOG_INFO, "OlcSchemaConfig::OlcSchemaConfig(const LDAPEntry &e1, const LDAPEntry &e2) : OlcConfigEntry(e1, e2)" );
    std::string name(this->getStringValue("cn"));
    std::string dn(e2.getDN() );
    if ( name.empty() )
    {
        throw std::runtime_error( "Entry '" + dn + "' has no 'cn' Attribute." );
    }
    if ( (dn.size() < schemabase.size()) || 
         (dn.compare( dn.size()-schemabase.size(), schemabase.size(), schemabase ))  )
    {
        throw std::runtime_error( "Entry '" + dn + "' is not a child of '" + schemabase + "'." );
    }
    if ( this->getStringValues("olcobjectclasses").empty() &&
         this->getStringValues("olcattributetypes").empty() )
    {
        throw std::runtime_error( "Entry '" + dn + "' does not define any objectclasses or attributetypes." );
    }

    entryIndex = splitIndexFromString( name, m_name );
}

void OlcSchemaConfig::clearChangedEntry()
{
    OlcConfigEntry::clearChangedEntry();
    m_name = "";
}

const std::string& OlcSchemaConfig::getName() const
{
    return m_name;
}

const std::vector<LDAPAttrType> OlcSchemaConfig::getAttributeTypes() const
{
    std::vector<LDAPAttrType> res;
    StringList types = this->getStringValues("olcAttributeTypes");
    StringList::const_iterator j;
    for ( j = types.begin(); j != types.end(); j++ )
    {
        LDAPAttrType currentAttr;
        std::string tmp;
        splitIndexFromString( *j, tmp );
        currentAttr = LDAPAttrType( tmp, LDAP_SCHEMA_ALLOW_NO_OID | 
                             LDAP_SCHEMA_ALLOW_QUOTED | LDAP_SCHEMA_ALLOW_OID_MACRO );
        res.push_back(currentAttr);
    }
    return res;
}

void OlcSchemaConfig::resetMemberAttrs()
{
    std::string name(this->getStringValue("cn"));
    entryIndex = splitIndexFromString( name, m_name );
}

void OlcSchemaConfig::updateEntryDn(bool origEntry )
{
    log_it(SLAPD_LOG_INFO, "updateEntryDN()");
    std::ostringstream dn, name;
    name << "{" << entryIndex << "}" << m_name;
    dn << "cn=" << name.str() << "," << "cn=schema,cn=config";
    m_dbEntryChanged.setDN(dn.str());
    m_dbEntryChanged.replaceAttribute(LDAPAttribute("cn", name.str()));
    if ( origEntry && (! m_dbEntry.getDN().empty()) )
    {
        m_dbEntry.setDN(dn.str());
        m_dbEntry.replaceAttribute(LDAPAttribute("cn", name.str()));
    }
}

OlcTlsSettings::OlcTlsSettings( const OlcGlobalConfig &ogc )
    : m_crlCheck(0), m_verifyCient(0)
{
    log_it(SLAPD_LOG_INFO,"OlcTlsSettings::OlcTlsSettings( const OlcGlobalConfig &ogc )" );
    std::string value = ogc.getStringValue("olcTLSCRLCheck");
    if ( value == "none" )
    {
        m_crlCheck = 0;
    }
    else if ( value == "peer" )
    {
        m_crlCheck = 1;
    }
    else if ( value == "all" )
    {
        m_crlCheck = 2;
    }
    value = ogc.getStringValue("olcTLSVerifyClient");
    if ( value == "never" )
    {
        m_verifyCient = 0;
    }
    else if ( value == "allow" )
    {
        m_verifyCient = 1;
    }
    else if ( value == "try" )
    {
        m_verifyCient = 2;
    }
    else if ( value == "demand" )
    {
        m_verifyCient = 3;
    }

    m_caCertDir = ogc.getStringValue("olcTlsCaCertificatePath");
    m_caCertFile = ogc.getStringValue("olcTlsCaCertificateFile");
    m_certFile = ogc.getStringValue("olcTlsCertificateFile");
    m_certKeyFile = ogc.getStringValue("olcTlsCertificateKeyFile");
    m_crlFile = ogc.getStringValue("olcTlsCrlFile");
}

void OlcTlsSettings::applySettings( OlcGlobalConfig &ogc ) const
{
    log_it(SLAPD_LOG_INFO,"OlcTlsSettings::applySettings( OlcGlobalConfig &ogc )" );
    ogc.setStringValue("olcTlsCaCertificatePath", m_caCertDir);
    ogc.setStringValue("olcTlsCaCertificateFile", m_caCertFile);
    ogc.setStringValue("olcTlsCertificateFile", m_certFile);
    ogc.setStringValue("olcTlsCertificateKeyFile", m_certKeyFile);
    ogc.setStringValue("olcTlsCrlFile", m_crlFile);
}

int OlcTlsSettings::getCrlCheck() const
{
    return m_crlCheck;
}

void OlcTlsSettings::setCrlCheck()
{
}

int OlcTlsSettings::getVerifyClient() const
{
    return m_verifyCient;
}

const std::string& OlcTlsSettings::getCaCertDir() const
{
    return m_caCertDir;
}

const std::string& OlcTlsSettings::getCaCertFile() const 
{
    return m_caCertFile;
}

const std::string& OlcTlsSettings::getCertFile() const 
{
    return m_certFile;
}
const std::string& OlcTlsSettings::getCertKeyFile() const 
{
    return m_certKeyFile;
}
const std::string& OlcTlsSettings::getCrlFile() const 
{
    return m_crlFile;
}

void OlcTlsSettings::setCaCertDir(const std::string& dir)
{
    m_caCertDir = dir;
}

void OlcTlsSettings::setCaCertFile(const std::string& file)
{
    m_caCertFile = file;
}

void OlcTlsSettings::setCertFile(const std::string& file)
{
    m_certFile = file;
}

void OlcTlsSettings::setCertKeyFile(const std::string& file)
{
    m_certKeyFile = file;
}

void OlcTlsSettings::setCrlFile(const std::string& file)
{
    m_crlFile = file;
}

OlcConfig::OlcConfig(LDAPConnection *lc) : m_lc(lc)
{
}

bool OlcConfig::hasConnection() const
{
    if ( m_lc )
    {
        return true;
    }
    else
    {
        return false;
    }
}

boost::shared_ptr<OlcGlobalConfig> OlcConfig::getGlobals()
{
    LDAPSearchResults *sr;
    LDAPEntry *dbEntry;
    if ( ! m_lc )
    {
        throw std::runtime_error( "LDAP Connection not initialized" );
    }
    try {
        sr = m_lc->search( "cn=config", LDAPConnection::SEARCH_BASE);
        dbEntry = sr->getNext();
    } catch (LDAPException e) {
        log_it(SLAPD_LOG_INFO, e.getResultMsg() + " " + e.getServerMsg() );
        throw;
    }
    if ( dbEntry ) {
        log_it(SLAPD_LOG_INFO,"Got GlobalConfig: " + dbEntry->getDN() );
        boost::shared_ptr<OlcGlobalConfig> gc( new OlcGlobalConfig(*dbEntry) );
        return gc;
    }
    boost::shared_ptr<OlcGlobalConfig> gc( new OlcGlobalConfig() );
    return gc;
}

void OlcConfig::setGlobals( OlcGlobalConfig &olcg)
{
    if ( ! m_lc )
    {
        throw std::runtime_error( "LDAP Connection not initialized" );
    }
    try {
        LDAPModList ml = olcg.entryDifftoMod();
        m_lc->modify( olcg.getDn(), &ml );
    } catch (LDAPException e) {
        log_it(SLAPD_LOG_INFO, e.getResultMsg() + " " + e.getServerMsg() );
        throw;
    }
}

void OlcConfig::updateEntry( OlcConfigEntry &oce )
{
    try {
        bool reread = true;
        log_it(SLAPD_LOG_INFO, "updateEntry() Old DN: "+oce.getDn()+" ChangedDN: "+ oce.getChangedEntry().getDN() );
//        log_it(SLAPD_LOG_INFO, " LDIF "+oce.toLdif() );
        if ( ! m_lc )
        {
            throw std::runtime_error( "LDAP Connection not initialized" );
        }
        if ( oce.isNewEntry () ) 
        {
            m_lc->add(&oce.getChangedEntry());
        } else if (oce.isDeletedEntry() ) {
            m_lc->del(oce.getDn());
            reread = false;
        } else {
            LDAPModList ml = oce.entryDifftoMod();
            if ( ! ml.empty() ) {
                m_lc->modify( oce.getDn(), &ml );
            } else {
                log_it(SLAPD_LOG_INFO, oce.getDn() + ": no changes" );
                reread = false;
            }
        }
        // re-read Entry from Server
        if ( reread )
        {
            LDAPSearchResults *sr = m_lc->search( oce.getUpdatedDn(), LDAPConnection::SEARCH_BASE);
            LDAPEntry *e = sr->getNext();
            if ( e ) {
                log_it(SLAPD_LOG_INFO,"Re-read Entry " + e->getDN() );
                oce.resetEntries( *e );
                delete(e);
            }
            delete(sr);
        }
    } catch (LDAPException e) {
        log_it(SLAPD_LOG_INFO, e.getResultMsg() + " " + e.getServerMsg() );
        throw;
    }
}

/*
 * This function triggers a simple Modify Operation ( basically a NO-OP) to
 * the config backend, if slapd is currently running an indexing task this
 * Operation will block until that task is finished as back-config can only
 * complete that operation when it's the only active thread.
 */
void OlcConfig::waitForBackgroundTasks()
{
    try {
        LDAPModification mod( LDAPAttribute("objectClass", "olcConfig"), LDAPModification::OP_ADD );
        LDAPModList ml;
        ml.addModification(mod);
        m_lc->modify( "cn=config", &ml );
    } catch (LDAPException e) {
        if (e.getResultCode() != LDAPResult::ATTRIBUTE_OR_VALUE_EXISTS )
        {
            log_it(SLAPD_LOG_INFO, e.getResultMsg() + " " + e.getServerMsg() );
            throw;
        }
    }
}

OlcDatabaseList OlcConfig::getDatabases()
{
    OlcDatabaseList res;
    if ( ! m_lc )
    {
        throw std::runtime_error( "LDAP Connection not initialized" );
    }
    try {
        LDAPSearchResults *sr = m_lc->search( "cn=config", 
                LDAPConnection::SEARCH_ONE, "objectclass=olcDatabaseConfig" );
        LDAPEntry *dbEntry;
        while ( (dbEntry = sr->getNext()) )
        {
            std::string dbDn(dbEntry->getDN());
            log_it(SLAPD_LOG_INFO,"Got Database Entry: " + dbDn);
            boost::shared_ptr<OlcDatabase> olce(OlcDatabase::createFromLdapEntry(*dbEntry));
            LDAPSearchResults *overlaySearchRes = m_lc->search( dbDn, 
                    LDAPConnection::SEARCH_ONE, "objectclass=olcOverlayConfig" );
            LDAPEntry *overlayEntry;
            while ( (overlayEntry = overlaySearchRes->getNext()) )
            {
                log_it(SLAPD_LOG_INFO,"Got Overlay: " + overlayEntry->getDN() );
                boost::shared_ptr<OlcOverlay> overlay(OlcOverlay::createFromLdapEntry(*overlayEntry) );
                olce->addOverlay(overlay);
            }
            res.push_back(olce);
        }
    } catch (LDAPException e ) {
        log_it(SLAPD_LOG_INFO, e.getResultMsg() + " " + e.getServerMsg() );
        throw;
    }
    return res;
}

OlcSchemaList OlcConfig::getSchemaNames()
{
    OlcSchemaList res;
    if ( ! m_lc )
    {
        throw std::runtime_error( "LDAP Connection not initialized" );
    }
    try {
        StringList attrs;
        LDAPSearchResults *sr = m_lc->search( "cn=schema,cn=config", 
                LDAPConnection::SEARCH_SUB, "objectclass=olcSchemaConfig" );
        LDAPEntry *entry;
        while ( (entry = sr->getNext()) )
        {
            log_it(SLAPD_LOG_INFO,"Got Schema Entry: " + entry->getDN() );
            boost::shared_ptr<OlcSchemaConfig> olce(new OlcSchemaConfig(*entry));
            res.push_back(olce);
        }
    } catch (LDAPException e ) {
        log_it(SLAPD_LOG_INFO, e.getResultMsg() + " " + e.getServerMsg() );
        throw;
    }
    return res;
}

void OlcConfig::setLogCallback( SlapdConfigLogCallback *lcb )
{
    OlcConfig::logCallback = lcb;
}


static void defaultLogCallback( int level, const std::string &msg,
            const char* file=0, const int line=0, const char* function=0)
{
    std::cerr << msg << std::endl;
}

SlapdConfigLogCallback *OlcConfig::logCallback = defaultLogCallback;

