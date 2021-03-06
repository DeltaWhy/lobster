// Copyright 2014 Wouter van Oortmerssen. All rights reserved.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

namespace lobster
{

struct NativeFun;
struct SymbolTable;
struct Node;

struct LineInfo : Serializable
{
    int line;
    int fileidx;
    int bytecodestart;

    LineInfo(int l, int i, int b) : line(l),  fileidx(i),  bytecodestart(b)  {}
    LineInfo() : LineInfo(-1, -1, -1) {}

    void Serialize(Serializer &ser)
    {
        ser(line);
        ser(fileidx);
        ser(bytecodestart);
    }
};

struct SubFunction;

struct Ident : Name
{
    int line;
    size_t scope;
    
    Ident *prev;

    SubFunction *sf_named;  // Surrounding named function (only used for @, remove me).
    SubFunction *sf_def;    // Where it is defined, including anonymous functions.
    
    bool single_assignment;
    bool constant;
    bool static_constant;
    bool anonymous_arg;

    int logvaridx;

    Type type;

    Ident(const string &_name, int _l, int _idx, size_t _sc)
        : Name(_name, _idx), line(_l), 
          scope(_sc), prev(nullptr), sf_named(nullptr), sf_def(nullptr),
          single_assignment(true), constant(false), static_constant(false), anonymous_arg(false), logvaridx(-1) {}
    Ident() : Ident("", -1, 0, SIZE_MAX) {}

    void Serialize(Serializer &ser)
    {
        Name::Serialize(ser);
        ser(line);
        ser(static_constant);
    }
    
    void Assign(Lex &lex)
    {
        single_assignment = false;
        if (constant)
            lex.Error("variable " + name + " is constant");
    }
};

struct FieldOffset
{
    int structidx, offset;

    FieldOffset(int _si, int _o) : structidx(_si), offset(_o) {}
    FieldOffset() : FieldOffset(-1, -1) {}
};

struct SharedField : Name
{
    vector<FieldOffset> offsets;

    int numunique;
    FieldOffset fo1, foN;    // in the case of 2 unique offsets where fo1 has only 1 occurence, and foN all the others
    int offsettable;         // in the case of N offsets (bytecode index)

    SharedField(const string &_name, int _idx) : Name(_name, _idx), numunique(0), offsettable(-1) {}
    SharedField() : SharedField("", 0) {}

    void NewFieldUse(const FieldOffset &nfo)
    {
        for (auto &fo : offsets) if (fo.offset == nfo.offset) goto found;
        numunique++;
        found:
        offsets.push_back(nfo);
    }
};

typedef Typed<SharedField> Field;

struct Struct : Name
{
    vector<Field> fields; 

    Struct *next;
    Struct *superclass;
    int superclassidx;
    
    bool readonly;
    bool typechecked;

    Type vectortype;  // What kind of vector this can be demoted to.

    Struct(const string &_name, int _idx)
        : Name(_name, _idx), next(nullptr), superclass(nullptr), superclassidx(-1), readonly(false), typechecked(false),
          vectortype(Type(V_VECTOR)) {}
    Struct() : Struct("", 0) {}

    void Serialize(Serializer &ser)
    {
        Name::Serialize(ser);
        ser(superclassidx);
        ser(readonly);
    }

    Field *Has(SharedField *fld)
    {
        for (auto &uf : fields) if (uf.id == fld) return &uf;
        return nullptr;
    }

    Struct *Clone()
    {
        auto st = new Struct();
        *st = *this;
        st->next = next;
        next = st;
        return st;
    }
};

struct Function;

struct SubFunction
{
    ArgVector args;
    ArgVector locals;
    ArgVector dynscoperedefs;  // any lhs of <-
    ArgVector freevars;        // any used from outside this scope, could overlap with dynscoperedefs
    vector<Type> returntypes;

    Node *body;

    SubFunction *next;
    Function *parent;

    int subbytecodestart;

    bool typechecked;

    SubFunction() 
        : parent(nullptr), args(0, nullptr), locals(0, nullptr), dynscoperedefs(0, nullptr), freevars(0, nullptr),
          body(nullptr), next(nullptr), subbytecodestart(0),
          typechecked(false)
    {
        returntypes.push_back(Type());  // functions always have at least 1 return value.
    }

    void SetParent(Function &f, SubFunction *&link)
    {
        parent = &f;
        next = link;
        link = this;
    }

    void CloneIds(SubFunction &o)
    {
        args = o.args;
        locals = o.locals;
        dynscoperedefs = o.dynscoperedefs;
        freevars = o.freevars;
    }

    ~SubFunction()
    {
        if (next) delete next;
    }
};

struct Function : Name
{
    int bytecodestart;

    SubFunction *subf; // functions with the same name and args, but different types (dynamic dispatch | specialization) 
    Function *sibf;    // functions with the same name but different number of args (overloaded)

    bool multimethod;  // if false, subfunctions can be generated by type specialization as opposed to programmer
                       // implemented dynamic dispatch

    bool anonymous;    // does not have a programmer specified name
    bool istype;       // its merely a function type, has no body, but does have a set return type.

    int scopelevel;
    int retvals;

    int ncalls;        // used by codegen to cull unused functions

    Function(const string &_name, int _idx, int _sl)
     : Name(_name, _idx), bytecodestart(0),  subf(nullptr), sibf(nullptr),
       multimethod(false), anonymous(false), istype(false), scopelevel(_sl), retvals(0), ncalls(0)
    {
    }
    Function() : Function("", 0, -1) {}
    ~Function() { if (subf) delete subf; }

    int nargs() { return (int)subf->args.v.size(); }

    void Serialize(Serializer &ser)
    {
        Name::Serialize(ser);
        ser(bytecodestart);
        ser(retvals);
    }
};

struct SymbolTable
{
    map<string, Ident *> idents;
    vector<Ident *> identtable;
    vector<Ident *> identstack;

    map<string, Struct *> structs;
    vector<Struct *> structtable;

    map<string, SharedField *> fields;
    vector<SharedField *> fieldtable;

    map<string, Function *> functions;
    vector<Function *> functiontable;

    vector<string> filenames;
    
    vector<size_t> scopelevels;

    vector<pair<Type, Ident *>> withstack;
    vector<size_t> withstacklevels;

    bool uses_frame_state;

    // Used during parsing.
    vector<SubFunction *> namedsubfunctionstack, defsubfunctionstack;

    SymbolTable() : uses_frame_state(false) {}

    ~SymbolTable()
    {
        for (auto id : identtable)    delete id;
        for (auto st : structtable)   delete st;
        for (auto f  : functiontable) delete f;
        for (auto f  : fieldtable)    delete f;
    }
    
    Ident *LookupDef(const string &name, int line, Lex &lex, bool anonymous_arg, bool islocal)
    {
        auto sf = defsubfunctionstack.empty() ? nullptr : defsubfunctionstack.back();

        auto it = idents.find(name);
        if (anonymous_arg && it != idents.end() && it->second->sf_def == sf) return it->second;

        Ident *ident = nullptr;
        if (LookupWithStruct(name, lex, ident))
            lex.Error("cannot define variable with same name as field in this scope: " + name);

        ident = new Ident(name, line, identtable.size(), scopelevels.back());
        ident->anonymous_arg = anonymous_arg;

        ident->sf_named = namedsubfunctionstack.empty() ? nullptr : namedsubfunctionstack.back();
        ident->sf_def = sf;
        if (sf) (islocal ? sf->locals : sf->args).v.push_back(ident);

        if (it == idents.end())
        {
            idents[name] = ident;
        }
        else
        {
            if (scopelevels.back() == it->second->scope) lex.Error("identifier redefinition: " + ident->name);
            ident->prev = it->second;
            it->second = ident;
        }
        identstack.push_back(ident);
        identtable.push_back(ident);
        return ident;
    }

    Ident *LookupDynScopeRedef(const string &name, Lex &lex)
    {
        auto it = idents.find(name);
        if (it == idents.end()) lex.Error("lhs of <- must refer to existing variable: " + name);
        if (defsubfunctionstack.size()) defsubfunctionstack.back()->dynscoperedefs.Add(it->second);
        return it->second;
    }
        
    Ident *LookupMaybe(const string &name)
    {
        auto it = idents.find(name);
        if (it == idents.end()) return nullptr;
        
        if (defsubfunctionstack.size() && it->second->sf_def != defsubfunctionstack.back())
        {
            // This is a free variable, record it in all parents up to the definition point.
            for (int i = (int)defsubfunctionstack.size() - 1; i >= 0; i--)
            {
                auto sf = defsubfunctionstack[i];
                if (it->second->sf_def == sf) break;  // Found the definition.
                sf->freevars.Add(it->second);
            }
        }
        return it->second;  
    }

    Ident *LookupUse(const string &name, Lex &lex)
    {
        auto id = LookupMaybe(name);
        if (!id)
            lex.Error("unknown identifier: " + name);
        return id;  
    }

    // FIXME: only used for @, and causes a lot of complexity, refactor?
    Ident *LookupIdentInFun(const string &idname, const string &fname) // slow, but infrequently used
    {
        Ident *found = nullptr;
        for (auto id : identtable)  
            if (id->name == idname && id->sf_named && id->sf_named->parent->name == fname)
            {
                if (found) return nullptr;
                found = id;
            }

        return found;
    }

    void AddWithStruct(Type &t, Ident *id, Lex &lex)
    {
        for (auto &wp : withstack) if (wp.first.idx == t.idx) lex.Error("type used twice in the same scope with ::");
        // FIXME: should also check if variables have already been defined in this scope that clash with the struct,
        // or do so in LookupUse
        assert(t.idx >= 0);
        withstack.push_back(make_pair(t, id));
    }

    SharedField *LookupWithStruct(const string &name, Lex &lex, Ident *&id)
    {
        auto fld = FieldUse(name);
        if (!fld) return nullptr;

        assert(!id);
        for (auto &wp : withstack)
        {
            if (structtable[wp.first.idx]->Has(fld))
            {
                if (id) lex.Error("access to ambiguous field: " + fld->name);
                id = wp.second;
            }
        }

        return id ? fld : nullptr;
    }
    
    void ScopeStart()
    {
        scopelevels.push_back(identstack.size());
        withstacklevels.push_back(withstack.size());
    }

    void ScopeCleanup()
    {
        while (identstack.size() > scopelevels.back())
        {
            auto ident = identstack.back();
            auto it = idents.find(ident->name);
            if (it != idents.end())   // can already have been removed by private var cleanup
            {
                if (ident->prev) it->second = ident->prev;
                else idents.erase(it);
            }
            
            identstack.pop_back();
        }
        scopelevels.pop_back();

        while (withstack.size() > withstacklevels.back()) withstack.pop_back();
        withstacklevels.pop_back();
    }

    void UnregisterStruct(const Struct *st)
    {
        auto it = structs.find(st->name);
        assert(it != structs.end());
        structs.erase(it);
    }

    void UnregisterFun(Function *f)
    {
        auto it = functions.find(f->name);
        if (it != functions.end())  // it can already have been remove by another variation
            functions.erase(it);
    }
    
    void EndOfInclude()
    {
        auto it = idents.begin();
        while (it != idents.end())
        {
            if (it->second->isprivate)
            {
                assert(!it->second->prev);
                idents.erase(it++);
            }
            else
                it++;
        }
    }

    Struct &StructDecl(const string &name, Lex &lex)
    {
        Struct *st = structs[name];
        if (st) lex.Error("double declaration of type: " + name);
        st = new Struct(name, structtable.size());
        structs[name] = st;
        structtable.push_back(st);
        return *st;
    }

    Struct &StructUse(const string &name, Lex &lex)
    {
        Struct *st = structs[name];
        if (!st) lex.Error("unknown type: " + name);
        return *st;
    }

    int StructIdx(const string &name, size_t &nargs) // FIXME: this is inefficient, used by parse_data()
    {
        for (auto s : structtable) if (s->name == name) 
        {
            nargs = s->fields.size();
            return s->idx;
        }
        return -1;
    }

    bool IsSuperTypeOrSame(int superidx, int subidx)
    {
        for (int t = subidx; t != -1; t = structtable[t]->superclassidx)
            if (t == superidx)
                return true;
        return false;
    }

    SharedField &FieldDecl(const string &name, int idx, Struct *st)
    {
        SharedField *fld = fields[name];
        if (!fld)
        {
            fld = new SharedField(name, fieldtable.size());
            fields[name] = fld;
            fieldtable.push_back(fld);
        }
        fld->NewFieldUse(FieldOffset(st->idx, idx));
        return *fld;
    }

    SharedField *FieldUse(const string &name)
    {
        auto it = fields.find(name);
        return it != fields.end() ? it->second : nullptr;
    }

    Function &CreateFunction(const string &name, const string &context)
    {
        auto fname = name.length() ? name : string("function") + inttoa(functiontable.size()) + context;
        auto f = new Function(fname, functiontable.size(), scopelevels.size());
        functiontable.push_back(f);
        return *f;
    }

    Function &FunctionDecl(const string &name, int nargs, Lex &lex)
    {
        auto fit = functions.find(name);

        if (fit != functions.end())
        {
            if (fit->second->scopelevel != int(scopelevels.size()))
                lex.Error("cannot define a variation of function " + name + " at a different scope level");

            for (auto f = fit->second; f; f = f->sibf)
                if (f->nargs() == nargs)
                    return *f;
        }

        auto &f = CreateFunction(name, "");

        if (fit != functions.end())
        {
            f.sibf = fit->second->sibf;
            fit->second->sibf = &f;
        }
        else
        {
            functions[name] = &f;
        }

        return f;
    }

    Function *FindFunction(const string &name)
    {
        auto it = functions.find(name);
        return it != functions.end() ? it->second : nullptr;
    }

    bool ReadOnlyIdent(uint v) { assert(v < identtable.size());    return identtable[v]->constant;  }
    bool ReadOnlyType (uint v) { assert(v < structtable.size());   return structtable[v]->readonly; }
    
    string &ReverseLookupIdent   (uint v) const { assert(v < identtable.size());    return identtable[v]->name;    }
    string &ReverseLookupType    (uint v) const { assert(v < structtable.size());   return structtable[v]->name;   }
    string &ReverseLookupFunction(uint v) const { assert(v < functiontable.size()); return functiontable[v]->name; }

    string TypeName(const Type &type, const Type *type_vars = nullptr) const
    {
        switch (type.t)
        {
            case V_STRUCT: return ReverseLookupType(type.idx).c_str();
            case V_VECTOR: return "[" + TypeName(type.Element(), type_vars) + "]";
            case V_FUNCTION: return type.idx < 0 // || functiontable[type.idx]->anonymous
                                ? "function"
                                : functiontable[type.idx]->name;
            case V_NILABLE: return TypeName(type.Element(), type_vars) + "?";
            case V_VAR: return type_vars ? TypeName(type_vars[type.idx], type_vars) + "*" : BaseTypeName(type.t);
            default: return BaseTypeName(type.t);
        }
    }

    void Serialize(Serializer &ser, vector<int> &code, vector<LineInfo> &linenumbers)
    {
        auto curvers = __DATE__; // __TIME__;
        string vers = curvers;
        ser(vers);
        if (ser.rbuf && vers != curvers) throw string("cannot load bytecode from a different version of the compiler");

        ser(uses_frame_state);

        ser(identtable);
        ser(functiontable);
        ser(structtable);
        ser(fieldtable);

        ser(code);
        ser(filenames);
        ser(linenumbers);
    }
};

}  // namespace lobster
