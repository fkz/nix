#pragma once

#include "attr-set.hh"
#include "value.hh"
#include "nixexpr.hh"
#include "symbol-table.hh"
#include "hash.hh"

#include <map>

#if HAVE_BOEHMGC
#include <gc/gc_allocator.h>
#endif


namespace nix {


class EvalState;

typedef enum {
    Normal,
    Record,
    Playback,
    RecordAndPlayback
} DeterministicEvaluationMode;

typedef void (* PrimOpFun) (EvalState & state, const Pos & pos, Value * * args, Value & v);

struct PrimOp
{
    PrimOpFun fun;
    unsigned int arity;
    Symbol name;
    PrimOp(PrimOpFun fun, unsigned int arity, Symbol name)
        : fun(fun), arity(arity), name(name) { }
};


struct Env
{
    Env * up;
    unsigned short size; // used by ‘valueSize’
    unsigned short prevWith:15; // nr of levels up to next `with' environment
    unsigned short haveWithAttrs:1;
    Value * values[0];
};


void mkString(Value & v, const string & s, const PathSet & context = PathSet());

void copyContext(const Value & v, PathSet & context);


/* Cache for calls to addToStore(); maps source paths to the store
   paths. */
typedef std::map<Path, Path> SrcToStore;


std::ostream & operator << (std::ostream & str, const Value & v);


typedef list<std::pair<string, Path> > SearchPath;


/* Initialise the Boehm GC, if applicable. */
void initGC();


class EvalState
{
public:
    SymbolTable symbols;

    const Symbol sWith, sOutPath, sDrvPath, sType, sMeta, sName, sValue,
        sSystem, sOverrides, sOutputs, sOutputName, sIgnoreNulls,
        sFile, sLine, sColumn, sFunctor, sToString;
    Symbol sDerivationNix;

    /* If set, force copying files to the Nix store even if they
       already exist there. */
    bool repair = false;

    /* If set, don't allow access to files outside of the Nix search
       path or to environment variables. */
    bool restricted;

    Value vEmptySet;

private:
    SrcToStore srcToStore;
    SrcToStore srcToStoreForPlayback;
    
    /* A cache from path names to values. */
#if HAVE_BOEHMGC
    typedef std::map<Path, Value, std::less<Path>, traceable_allocator<std::pair<const Path, Value> > > FileEvalCache;
#else
    typedef std::map<Path, Value> FileEvalCache;
#endif
    FileEvalCache fileEvalCache;

    SearchPath searchPath;

    DeterministicEvaluationMode evalMode;

    //TODO: use some other structure, maybe a hashmap
    //since comparing strings with long same prefixes is slow
    std::map<std::pair<string, std::list<string>>, Value> recording;
    
public:
    bool isInPlaybackMode() {
        return evalMode == Playback || evalMode == RecordAndPlayback;
    }
  

    EvalState(const Strings & _searchPath, DeterministicEvaluationMode evalMode = Normal);
    ~EvalState();

    void addToSearchPath(const string & s, bool warn = false);
    void addPlaybackSubstitutions(nix::Value& top); 
    void addPlaybackSource(const Path & from, const Path & to);
    
    Path checkSourcePath(const Path & path);

    /* Parse a Nix expression from the specified file. */
    Expr * parseExprFromFile(const Path & path);
    Expr * parseExprFromFile(const Path & path, StaticEnv & staticEnv);
    Expr * parseExprFromFileWithoutRecording(const Path & path);
    Expr * parseExprFromFileWithoutRecording(const Path & path, StaticEnv &);
    
    /* Parse a Nix expression from the specified string. */
    Expr * parseExprFromString(const string & s, const Path & basePath, StaticEnv & staticEnv);
    Expr * parseExprFromString(const string & s, const Path & basePath);

    /* Evaluate an expression read from the given file to normal
       form. */
    void evalFile(const Path & path, Value & v);

    void resetFileCache();

    /* Look up a file in the search path. */
    Path findFile(const string & path);
    Path findFile(SearchPath & searchPath, const string & path, const Pos & pos = noPos);

    /* Evaluate an expression to normal form, storing the result in
       value `v'. */
    void eval(Expr * e, Value & v);
    
    // convert a value back to an expression
    Expr * valueToExpression(const Value & v);

    /* Evaluation the expression, then verify that it has the expected
       type. */
    inline bool evalBool(Env & env, Expr * e);
    inline bool evalBool(Env & env, Expr * e, const Pos & pos);
    inline void evalAttrs(Env & env, Expr * e, Value & v);

    /* If `v' is a thunk, enter it and overwrite `v' with the result
       of the evaluation of the thunk.  If `v' is a delayed function
       application, call the function and overwrite `v' with the
       result.  Otherwise, this is a no-op. */
    inline void forceValue(Value & v, const Pos & pos = noPos);

    /* Force a value, then recursively force list elements and
       attributes. */
    void forceValueDeep(Value & v);

    /* Force `v', and then verify that it has the expected type. */
    NixInt forceInt(Value & v, const Pos & pos);
    bool forceBool(Value & v);
    inline void forceAttrs(Value & v);
    inline void forceAttrs(Value & v, const Pos & pos);
    inline void forceList(Value & v);
    inline void forceList(Value & v, const Pos & pos);
    void forceFunction(Value & v, const Pos & pos); // either lambda or primop
    string forceString(Value & v, const Pos & pos = noPos);
    string forceString(Value & v, PathSet & context, const Pos & pos = noPos);
    string forceStringNoCtx(Value & v, const Pos & pos = noPos);

    /* Return true iff the value `v' denotes a derivation (i.e. a
       set with attribute `type = "derivation"'). */
    bool isDerivation(Value & v);

    /* String coercion.  Converts strings, paths and derivations to a
       string.  If `coerceMore' is set, also converts nulls, integers,
       booleans and lists to a string.  If `copyToStore' is set,
       referenced paths are copied to the Nix store as a side effect. */
    string coerceToString(const Pos & pos, Value & v, PathSet & context,
        bool coerceMore = false, bool copyToStore = true);

    string copyPathToStore(PathSet & context, const Path & path, bool ignoreReadOnly = false);

    /* Path coercion.  Converts strings, paths and derivations to a
       path.  The result is guaranteed to be a canonicalised, absolute
       path.  Nothing is copied to the store. */
    Path coerceToPath(const Pos & pos, Value & v, PathSet & context);

public:

    /* The base environment, containing the builtin functions and
       values. */
    Env & baseEnv;

    /* The same, but used during parsing to resolve variables. */
    StaticEnv staticBaseEnv; // !!! should be private

private:

    unsigned int baseEnvDispl = 0;

    void createBaseEnv();

    void addConstant(const string & name, Value & v);
    void addImpureConstant(const string & name, Value & v, Value * impureConstant);
    
    void addPrimOp(const string & name,
        unsigned int arity, PrimOpFun primOp);

    std::string valueToJSON(Value & value, bool copyToStore);
    string parameterValue(Value & value);
    void getAttr(Value & top, const Symbol & arg2, Value & v);
    void initializeDeterministicEvaluationMode();
 
    static bool constTrue(unsigned int arg) { return true; }
    template< int argumentPos >
    static bool onlyPos(unsigned int arg) { return arg == argumentPos; }
    typedef bool (*UsedArguments) (unsigned int argumentIndex);
    
    template< const char * name, unsigned int arity, PrimOpFun primOp, UsedArguments useArgument >
    static void recordPrimOp(EvalState & state, const Pos & pos, Value * * args, Value & v)
    {
        std::list<string> argList;
        for (unsigned int i = 0; i < arity; i++) {
            if (useArgument(i)) {
                argList.push_back(state.parameterValue(*args[i]));
            }
        }
        primOp(state, pos, args, v);
        state.recording[std::make_pair(name, argList)] = v;
    }

    template< const char * name, unsigned int arity, PrimOpFun primOp, UsedArguments useArgument >
    static void playbackPrimOp(EvalState & state, const Pos & pos, Value * * args, Value & v)
    {
        std::list<string> argList;
        for (unsigned int i = 0; i < arity; i++) {
            if(useArgument(i)) {
                argList.push_back(state.parameterValue(*args[i]));
            }
        }
        auto result = state.recording.find(std::make_pair(name, argList));
        if (result == state.recording.end()) {
            std::string errorMsg("wanted to call ");
            errorMsg += name;
            errorMsg += "(";
            for (auto arg: argList) {
                errorMsg += arg + ", ";
            }
            errorMsg += ")";
            throw EvalError(errorMsg.c_str());
        }
        else {
            v = result->second;
        }
    }
    
    template< const char * name, unsigned int arity, PrimOpFun primOp, UsedArguments useArguments >
    PrimOpFun transformPrimOp()
    {
        switch (evalMode) {
            case Record: return recordPrimOp<name, arity, primOp, useArguments>;
            case Playback: return playbackPrimOp<name, arity, primOp, useArguments>;
            case Normal: return primOp;
            default: abort();
        }
    }
    
    template< const char * name, unsigned int arity, PrimOpFun primOp, UsedArguments useArguments = constTrue>
    void addImpurePrimOp() 
    {
        addPrimOp(name, arity, transformPrimOp<name, arity, primOp, useArguments>());
    }
    
    template< const char * name >
    static void unsupportedPrimOp(EvalState & state, const Pos & pos, Value * * args, Value & v)
    {
        throw EvalError(format("primop '%s' is not (yet) supported in Record/Playback mode (used at '%s')") % name % pos);
    }
    
    template< const char * name >
    void addUnsupportedImpurePrimOp(unsigned int arity, PrimOpFun primOp)
    {
        if (evalMode == Record || evalMode == Playback) {
            addPrimOp(name, arity, unsupportedPrimOp<name>);
        }
        else {
            addPrimOp(name, arity, primOp);
        }
    }

public:
    void finalizeRecording (Value & result, Expr * recordingExpressions);
    Path writeRecordingIntoStore (Value & result, bool buildStorePath);

    void getBuiltin(const string & name, Value & v);

private:

    inline Value * lookupVar(Env * env, const ExprVar & var, bool noEval);

    friend struct ExprVar;
    friend struct ExprAttrs;
    friend struct ExprLet;

    Expr * parse(const char * text, const Path & path,
        const Path & basePath, StaticEnv & staticEnv);

public:

    /* Do a deep equality test between two values.  That is, list
       elements and attributes are compared recursively. */
    bool eqValues(Value & v1, Value & v2);

    bool isFunctor(Value & fun);

    void callFunction(Value & fun, Value & arg, Value & v, const Pos & pos);
    void callPrimOp(Value & fun, Value & arg, Value & v, const Pos & pos);

    /* Automatically call a function for which each argument has a
       default value or has a binding in the `args' map. */
    void autoCallFunction(Bindings & args, Value & fun, Value & res);

    /* Allocation primitives. */
    Value * allocValue();
    Env & allocEnv(unsigned int size);

    Value * allocAttr(Value & vAttrs, const Symbol & name);

    Bindings * allocBindings(Bindings::size_t capacity);

    void mkList(Value & v, unsigned int length);
    void mkAttrs(Value & v, unsigned int capacity);
    void mkThunk_(Value & v, Expr * expr);
    void mkPos(Value & v, Pos * pos);

    void concatLists(Value & v, unsigned int nrLists, Value * * lists, const Pos & pos);

    /* Print statistics. */
    void printStats();

private:

    unsigned long nrEnvs = 0;
    unsigned long nrValuesInEnvs = 0;
    unsigned long nrValues = 0;
    unsigned long nrListElems = 0;
    unsigned long nrAttrsets = 0;
    unsigned long nrAttrsInAttrsets = 0;
    unsigned long nrOpUpdates = 0;
    unsigned long nrOpUpdateValuesCopied = 0;
    unsigned long nrListConcats = 0;
    unsigned long nrPrimOpCalls = 0;
    unsigned long nrFunctionCalls = 0;

    bool countCalls;

    typedef std::map<Symbol, unsigned int> PrimOpCalls;
    PrimOpCalls primOpCalls;

    typedef std::map<ExprLambda *, unsigned int> FunctionCalls;
    FunctionCalls functionCalls;

    void incrFunctionCall(ExprLambda * fun);

    typedef std::map<Pos, unsigned int> AttrSelects;
    AttrSelects attrSelects;

    friend struct ExprOpUpdate;
    friend struct ExprOpConcatLists;
    friend struct ExprSelect;
    friend void prim_getAttr(EvalState & state, const Pos & pos, Value * * args, Value & v);
    void addToBaseEnv(const string & name, Value * v, Symbol sym);
    void addToBaseEnv(const string & name, Value * v);
    Value * getImpureConstantPrimop();
    Path copyPathToStoreIfItsNotAlreadyThere(PathSet & context, Path path);
};


/* Return a string representing the type of the value `v'. */
string showType(const Value & v);


/* If `path' refers to a directory, then append "/default.nix". */
Path resolveExprPath(Path path);

struct InvalidPathError : EvalError
{
    Path path;
    InvalidPathError(const Path & path);
#ifdef EXCEPTION_NEEDS_THROW_SPEC
    ~InvalidPathError() throw () { };
#endif
};

/* Realise all paths in `context' */
void realiseContext(const PathSet & context);

}
