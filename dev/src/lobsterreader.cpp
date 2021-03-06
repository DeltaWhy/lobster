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

#include "stdafx.h"

#include "vmdata.h"
#include "natreg.h"

#include "ttypes.h"
#include "lex.h"

using namespace lobster;

struct ValueParser
{
    vector<string> filenames;
    vector<RefObj *> allocated;
    Lex lex;

    ValueParser(char *_src) : lex("string", filenames, _src)
    {
    }

    ~ValueParser()
    {
        for (auto lo : allocated)
            Value(lo).DECRT();
    }

    Value Parse()
    {
        Value v = ParseFactor();
        Gobble(T_LINEFEED);
        Expect(T_ENDOFFILE);
        return v;
    }

    Value ParseFactor()
    {
        switch (lex.token)
        {
            case T_INT:   { int i    = atoi(lex.sattr.c_str()); lex.Next(); return Value(i); }
            case T_FLOAT: { double f = atof(lex.sattr.c_str()); lex.Next(); return Value((float)f); }
            case T_STR:   { string s = lex.sattr;               lex.Next(); auto str = g_vm->NewString(s);
                                                                            allocated.push_back(str);
                                                                            return Value(str); }     
            case T_NIL:   {                                     lex.Next(); return Value(0, V_NIL); }

            case T_MINUS:
            {
                lex.Next(); 
                Value v = ParseFactor();
                switch (v.type)
                {
                    case V_INT:   v.ival *= -1; break;
                    case V_FLOAT: v.fval *= -1; break;
                    default: lex.Error("numeric value expected");
                }
                return v;
            }

            case T_LEFTBRACKET:
            {
                lex.Next();
                Gobble(T_LINEFEED);
                vector<Value> elems;
                if (lex.token == T_RIGHTBRACKET) lex.Next();
                else
                {
                    for (;;)
                    {
                        elems.push_back(ParseFactor());
                        bool haslf = lex.token == T_LINEFEED;
                        if (haslf) lex.Next();
                        if (lex.token == T_RIGHTBRACKET) break;
                        if (!haslf) Expect(T_COMMA);
                    }
                    lex.Next();
                }

                int type = -1;
                if (lex.token == T_COLON)
                {
                    lex.Next();
                    string sname = lex.sattr;
                    Expect(T_IDENT);
                    size_t reqargs = 0;
                    int idx = g_vm->StructIdx(sname, reqargs);
                    if (idx >= 0)   // if unknown type, becomes regular vector
                    {
                        // pad with NIL if current type has more fields
                        while (elems.size() < reqargs) { elems.push_back(Value(0, V_NIL)); }
                        // drop elements if current type has less fields
                        while (elems.size() > reqargs) { elems.back().DEC(); elems.pop_back(); }
                        type = idx;
                    }
                }

                auto vec = g_vm->NewVector(elems.size(), type);
                allocated.push_back(vec);
                for (auto &e : elems) vec->push(e.INC());
                return Value(vec);
            }

            default:
                lex.Error("illegal start of expression: " + lex.TokStr());
                return Value();
        }
    }

    void Expect(TType t)
    {
        if (lex.token != t)
            lex.Error(lex.TokStr(t) + " expected, found: " + lex.TokStr());

        lex.Next();
    }

    void Gobble(TType t)
    {
        if (lex.token == t) lex.Next();
    }
};

static Value ParseData(char *inp)
{
    try
    {
        ValueParser parser(inp);
        g_vm->Push(parser.Parse().INC());
        return Value(0, V_NIL);
    }
    catch (string &s)
    {
        g_vm->Push(Value(0, V_NIL));
        return Value(g_vm->NewString(s));
    }
}

void AddReaderOps()
{
    STARTDECL(parse_data) (Value &ins)
    {
        Value v = ParseData(ins.sval->str());
        ins.DEC();
        return v;
    }
    ENDDECL1(parse_data, "stringdata", "S", "AA",
        "parses a string containing a data structure in lobster syntax (what you get if you convert an arbitrary data"
        " structure to a string) back into a data structure. supports int/float/string/vector and structs."
        " structs will be forced to be compatible with their current definitions, i.e. too many elements will be"
        " truncated, missing elements will be set to nil, and unknown type means downgrade to vector."
        " useful for simple file formats. returns the value and an error string as second return value"
        " (or nil if no error)");
}

AutoRegister __aro("parsedata", AddReaderOps);


