// thx https://github.com/jbro129/Unity-Substrate-Hook-Android

#include <jni.h>
#include <sys/types.h>
#include <sys/stat.h>

#include "utils.h"
#include <android/log.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <dlfcn.h>
#include <unistd.h>
#include <iostream>

using namespace std;
using namespace rapidjson;

long baseAddr(char *soname)  // credits to https://github.com/ikoz/AndroidSubstrate_hookingC_examples/blob/master/nativeHook3/jni/nativeHook3.cy.cpp
{
    void *imagehandle = dlopen(soname, RTLD_LOCAL | RTLD_LAZY);
    if (soname == NULL)
        return NULL;
    if (imagehandle == NULL){
        return NULL;
    }
    uintptr_t * irc = NULL;
    FILE *f = NULL;
    char line[200] = {0};
    char *state = NULL;
    char *tok = NULL;
    char * baseAddr = NULL;
    if ((f = fopen("/proc/self/maps", "r")) == NULL)
        return NULL;
    while (fgets(line, 199, f) != NULL)
    {
        tok = strtok_r(line, "-", &state);
        baseAddr = tok;
        tok = strtok_r(NULL, "\t ", &state);
        tok = strtok_r(NULL, "\t ", &state); // "r-xp" field
        tok = strtok_r(NULL, "\t ", &state); // "0000000" field
        tok = strtok_r(NULL, "\t ", &state); // "01:02" field
        tok = strtok_r(NULL, "\t ", &state); // "133224" field
        tok = strtok_r(NULL, "\t ", &state); // path field

        if (tok != NULL) {
            int i;
            for (i = (int)strlen(tok)-1; i >= 0; --i) {
                if (!(tok[i] == ' ' || tok[i] == '\r' || tok[i] == '\n' || tok[i] == '\t'))
                    break;
                tok[i] = 0;
            }
            {
                size_t toklen = strlen(tok);
                size_t solen = strlen(soname);
                if (toklen > 0) {
                    if (toklen >= solen && strcmp(tok + (toklen - solen), soname) == 0) {
                        fclose(f);
                        return (long)strtoll(baseAddr,NULL,16);
                    }
                }
            }
        }
    }
    fclose(f);
    return NULL;
}

long location; // save lib.so base address so we do not have to recalculate every time causing lag.

long getRealOffset(long offset) // calculate dump.cs address + lib.so base address.
{
    if (location == 0)
    {
        //arm
        location = baseAddr("/data/app/com.beatgames.beatsaber-1/lib/arm/libil2cpp.so"); // replace the com.package.name with the package name of the app you are modding.
    }
    return location + offset;
}

// BEAT SABER SPECIFIC

// il2cpp_string_new, used to find string construction offset: 0x2DD144
// il2cpp_string_new immediate call offset: 0x30A1C8
// Creation of string method(char* chars, size_t length): 0x30A1E8
static const long NEW_STRING_OFFSET = 0x30A1E8;

cs_string* createcsstr(char* characters, size_t length) {
    cs_string* (*create_str)(char*, size_t) = (void*)getRealOffset(NEW_STRING_OFFSET);
    return create_str(characters, length);
}

void csstrtowstr(cs_string* in, unsigned short* out)
{
    for(int i = 0; i < in->len; i++) {
        out[i] = in->str[i];
    }
}

void setcswstr(cs_string* in, unsigned short* value, size_t length) {
    unsigned int l = (unsigned int)length;
    in->len = l;
    for(int i = 0; i < l; i++) {
        in->str[i] = value[i];
    }
}

void setcsstr(cs_string* in, char* value, size_t length) {
    unsigned int l = (unsigned int)length;
    in->len = l;
    for(int i = 0; i < l; i++) {
        // Can assume that each char is only a single char (a single word --> double word)
        in->str[i] = (unsigned short)value[i];
    }
}

void csstrtostr(cs_string* in, char* out)
{
    for(int i = 0; i < in->len; i++) {
        // Can assume that each short can just be a literal char
        out[i*2] = in->str[i];
    }
    out[in->len] = '\x0';
}

// BEAT SABER SETTINGS

bool fileexists(const char* filename) {
    return access(filename, W_OK | R_OK) != -1;
}

bool direxists(const char* dirname) {
    struct stat info;

    if (stat(dirname, &info) != 0) {
        return false;
    } if (info.st_mode & S_IFDIR) {
        return true;
    }
    return false;
}

char* readfile(const char* filename) {
    FILE* fp = fopen(filename, "r");
    char* content = NULL;
    long size = 0;
    if (fp) {
        fseek(fp, 0, SEEK_END);
        size = ftell(fp);
        rewind(fp);
        content = (char*)malloc(size * sizeof(char));
        fread(content, sizeof(char), size, fp);
        fclose(fp);
    }
    return content;
}

int writefile(const char* filename, const char* text) {
    FILE* fp = fopen(filename, "w");
    if (fp) {
        fwrite(text, sizeof(char), strlen(text), fp);
        fclose(fp);
        return 0;
    }
    return WRITE_ERROR_COULD_NOT_MAKE_FILE;
}

rapidjson::Document parsejson(string_view js) {
    char temp[js.length()];
    memcpy(temp, js.data(), js.length());
    
    Document doc;
    if (doc.ParseInsitu(temp).HasParseError()) {
        return {};
    }
    return doc;
}

rapidjson::Document parsejsonfile(string filename) {
    if (!fileexists(filename.c_str())) {
        return {};
    }
    FILE* fp = fopen(filename.c_str(), "r");
    
    Document doc;
    if (doc.ParseStream(fp).HasParseError()) {
        return {};
    }
    return doc;
}

// CONFIG

rapidjson::Document config_object;
bool readJson = false;

// Loads the config for the given MOD_ID, if it doesn't exist, will leave it as an empty object.
void loadConfig() {
    if (!direxists(CONFIG_PATH)) {
        mkdir(CONFIG_PATH, 0700);
    }
    string filename = CONFIG_PATH + MOD_ID + ".json";

    if (!fileexists(filename.c_str())) {
        writefile(filename.c_str(), "{}");
    }
    config_object = parsejsonfile(filename);
    if (!config_object.IsObject()) {
        config_object.SetObject();
    }
    readJson = true;
}

void writeConfig() {
    if (!direxists(CONFIG_PATH)) {
        mkdir(CONFIG_PATH);
    }
    string filename = CONFIG_PATH + MOD_ID + ".json";

    StringBuffer buf;
    PrettyWriter<StringBuffer> writer(buf);
    config_object.Accept(writer);
    writefile(filename.c_str(), buf.GetString());
}

tao::json::value getconfigvalue(const char* key, tao::json::value defaultValue = NULL, bool insertIfNotFound = false) {
    if (!readJson) {
        loadConfig();
    }
    tao::json::value v = config_object.find(key);
    if (v) {
        return v;
    } else {
        if (insertIfNotFound) {
            setconfigvalue(key, defaultValue);
        }
        return defaultValue;
    }
}

tao::json::value setconfigvalue(const char* key, tao::json::value value) {
    if (!readJson) {
        loadConfig();
    }
    config_object[key] = value;
    writeConfig();
}