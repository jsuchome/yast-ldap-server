#ifndef BACK_CONFIG_TEST_H
#define BACK_CONFIG_TEST_H
#include <LDAPConnection.h>
#include <LDAPResult.h>
#include <string>
#include <iostream>
#include <sstream>
#include <map>
#include <vector>
#include <LDAPEntry.h>
#include <LDAPAttrType.h>
#include <boost/shared_ptr.hpp>

class OlcConfigEntry
{
    public:
        static OlcConfigEntry* createFromLdapEntry( const LDAPEntry& le);
        static bool isDatabaseEntry( const LDAPEntry& le);
        static bool isScheamEntry( const LDAPEntry& le);
        static bool isOverlayEntry( const LDAPEntry& le);
        static bool isGlobalEntry( const LDAPEntry& le);

        inline OlcConfigEntry() : m_dbEntry(), m_dbEntryChanged() {}
        inline OlcConfigEntry(const LDAPEntry& le) : m_dbEntry(le), m_dbEntryChanged(le) {}
        inline OlcConfigEntry(const LDAPEntry& le, const LDAPEntry& le1) : m_dbEntry(le), m_dbEntryChanged(le1) {}
        inline std::string getDn() const { 
            return m_dbEntry.getDN();
        }
        inline std::string getUpdatedDn() const { 
            return m_dbEntryChanged.getDN();
        }
        inline const LDAPEntry& getChangedEntry() const {
            return m_dbEntryChanged;
        }
        virtual void clearChangedEntry();     

        bool isNewEntry() const;
        bool isDeletedEntry() const;

        LDAPModList entryDifftoMod() const;
        
        StringList getStringValues(const std::string &type) const;
        void setStringValues(const std::string &type, const StringList &values);

        // shortcuts for single-valued Attributes
        std::string getStringValue(const std::string &type) const;
        void setStringValue(const std::string &type, const std::string &value);
        void addStringValue(const std::string &type, const std::string &value);

        void setIndex( int index );

        int getEntryIndex() const;

//        virtual std::map<std::string, std::list<std::string> > toMap() const;
        virtual std::string toLdif() const;

    protected:
        virtual void updateEntryDn();

        int entryIndex;
        LDAPEntry m_dbEntry;
        LDAPEntry m_dbEntryChanged;
};

enum IndexType {
    Default,
    Present,
    Eq,
    Approx,
    Sub,
    SpecialSubInitial,
    SpecialSubAny,
    SpecialSubFinal,
    SpecialNoLang,
    SpecialNoSubTypes,
};

typedef std::map<std::string, std::vector<IndexType> > IndexMap;

class OlcOverlay : public OlcConfigEntry
{
    public:
        static OlcOverlay* createFromLdapEntry( const LDAPEntry& le);
        OlcOverlay( const LDAPEntry &le );
        const std::string getType() const;

    protected:
        std::string m_type;
};

typedef std::list<boost::shared_ptr<OlcOverlay> > OlcOverlayList;
class OlcDatabase : public OlcConfigEntry
{
    public :
        static OlcDatabase* createFromLdapEntry( const LDAPEntry& le );
        
        OlcDatabase( const LDAPEntry &le );
        OlcDatabase( const std::string& type );

        static bool isBdbDatabase( const LDAPEntry& le );
        
        void setSuffix( const std::string &suffix);
        void setRootDn( const std::string &rootdn);
        void setRootPw( const std::string &rootpw);

        const std::string getSuffix() const;
        const std::string getType() const;

        //virtual std::map<std::string, std::list<std::string> > toMap() const;
        virtual IndexMap getDatabaseIndexes() const {};
        virtual std::vector<IndexType> getDatabaseIndex( const std::string &attr ) const {};
        virtual void addIndex(const std::string& attr, const std::vector<IndexType>& idx) {};
        virtual void deleteIndex(const std::string& attr) {};

        void addOverlay(boost::shared_ptr<OlcOverlay> overlay);
        OlcOverlayList& getOverlays() ;

    protected:
        virtual void updateEntryDn();
        std::string m_type;
        OlcOverlayList m_overlays;

};

class OlcBdbDatabase : public  OlcDatabase 
{
    public:
        OlcBdbDatabase();
        OlcBdbDatabase( const LDAPEntry& le );
        //virtual std::map<std::string, std::list<std::string> > toMap() const;
        void setDirectory( const std::string &dir);

        virtual IndexMap getDatabaseIndexes() const;
        virtual std::vector<IndexType> getDatabaseIndex( const std::string &attr ) const;
        virtual void addIndex(const std::string& attr, const std::vector<IndexType>& idx);
        virtual void deleteIndex(const std::string& attr);
};

class OlcTlsSettings;

class OlcGlobalConfig : public OlcConfigEntry 
{
    public:
        OlcGlobalConfig();
        explicit OlcGlobalConfig( const LDAPEntry &le);

        const std::vector<std::string> getLogLevelString() const;
        void setLogLevel(const std::list<std::string> &level);
        void addLogLevel(std::string level);

        const std::vector<std::string> getAllowFeatures() const;
        void setAllowFeatures( const std::list<std::string> &features );
        const std::vector<std::string> getDisallowFeatures() const;
        void setDisallowFeatures( const std::list<std::string> &features );

        OlcTlsSettings getTlsSettings() const;
        void setTlsSettings( const OlcTlsSettings& tls);
        //virtual std::map<std::string, std::list<std::string> > toMap() const;
};

class OlcSchemaConfig : public OlcConfigEntry
{
    public:
        OlcSchemaConfig();
        OlcSchemaConfig(const LDAPEntry &e);
        OlcSchemaConfig(const LDAPEntry &e1, const LDAPEntry &e2);
        virtual void clearChangedEntry();     
        const std::string& getName() const;
        const std::vector<LDAPAttrType> getAttributeTypes() const;

    private:
        std::string m_name;
};

class OlcTlsSettings {
    public :
        OlcTlsSettings( const OlcGlobalConfig &ogc );

        void applySettings( OlcGlobalConfig &ogc ) const;

        int getCrlCheck() const;
        int getVerifyClient() const;
        const std::string& getCaCertDir() const;
        const std::string& getCaCertFile() const;
        const std::string& getCertFile() const;
        const std::string& getCertKeyFile() const;
        const std::string& getCrlFile() const;

        void setCrlCheck();
        void setVerifyClient();
        void setCaCertDir(const std::string& dir);
        void setCaCertFile(const std::string& file);
        void setCertFile(const std::string& file);
        void setCertKeyFile(const std::string& file);
        void setCrlFile(const std::string& file);

    private:
        int m_crlCheck;
        int m_verifyCient;
        std::string m_caCertDir;
        std::string m_caCertFile;
        std::string m_certFile;
        std::string m_certKeyFile;
        std::string m_crlFile;
};

typedef std::list<boost::shared_ptr<OlcDatabase> > OlcDatabaseList;
typedef std::list<boost::shared_ptr<OlcSchemaConfig> > OlcSchemaList;

class OlcConfig {

    public:
        OlcConfig(LDAPConnection *lc=0 );

        boost::shared_ptr<OlcGlobalConfig> getGlobals();
        OlcDatabaseList getDatabases();
        OlcSchemaList getSchemaNames();

        void setGlobals( OlcGlobalConfig &olcg);
        void updateEntry( const OlcConfigEntry &oce );

    private:
        LDAPConnection *m_lc;
};


#endif /* BACK_CONFIG_TEST_H */
