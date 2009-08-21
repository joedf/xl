#ifndef CONTEXT_H
#define CONTEXT_H
// ****************************************************************************
//  context.h                       (C) 1992-2003 Christophe de Dinechin (ddd) 
//                                                                 XL2 project 
// ****************************************************************************
// 
//   File Description:
// 
//     The execution environment for XL
// 
// 
// 
// 
// 
// 
// 
// 
// ****************************************************************************
// This program is released under the GNU General Public License.
// See http://www.gnu.org/copyleft/gpl.html for details
// ****************************************************************************
// * File       : $RCSFile$
// * Revision   : $Revision$
// * Date       : $Date$
// ****************************************************************************

#include <map>
#include <set>
#include <vector>
#include "base.h"

XL_BEGIN

struct Tree;
struct Variable;
struct Action;
struct Context;
struct Errors;
struct Rewrite;

typedef std::map<text, Tree *>    symbol_table;
typedef std::set<Tree *>          active_set;
typedef std::map<ulong, Rewrite*> rewrite_table;
typedef std::map<Tree*, Tree*>    compile_cache;


struct Namespace
// ----------------------------------------------------------------------------
//   Holds the symbols in a given context
// ----------------------------------------------------------------------------
{
    Namespace(Namespace *p): parent(p), rewrites(NULL), numVars(0) {}
    ~Namespace();

    Namespace *         Parent()                { return parent; }

    // Symbol management
    Tree *              Name (text name)        { return names[name]; }
    Rewrite *           Rewrites()              { return rewrites; }

    // Entering symbols in the symbol table
    void                EnterName (text name, Tree *value);
    Rewrite *           EnterRewrite(Rewrite *r);
    Variable *          AllocateVariable (text name, ulong treepos);

    // Clearing symbol tables
    void                Clear();

public:
    Namespace *         parent;
    symbol_table        names;
    Rewrite *           rewrites;
    ulong               numVars;
};


struct Context : Namespace
// ----------------------------------------------------------------------------
//   The compilation context in which we evaluate trees
// ----------------------------------------------------------------------------
{
    // Constructors and destructors
    Context(Errors &err):
        Namespace(NULL),
        errors(err), gc_threshold(200), error_handler(NULL) {}
    Context(Context *p):
        Namespace(p),
        errors(p->errors),gc_threshold(200),error_handler(NULL) {}

    // Context properties
    Context *           Parent()                 { return (Context *) parent;}
    Tree *              ErrorHandler();
    void                SetErrorHandler(Tree *e) { error_handler = e; }
    
    // Garbage collection
    void                Root(Tree *t)           { roots.insert(t); }
    void                Mark(Tree *t)           { active.insert(t); }
    void                CollectGarbage();

    // Evaluation of trees
    Tree *              Compile(Tree *source);
    Tree *              Run(Tree *source);
    Rewrite *           EnterRewrite(Tree *from, Tree *to);
    Tree *              Error (text message,
                               Tree *a1=NULL, Tree *a2=NULL, Tree *a3=NULL);

public:
    static ulong        gc_increment;
    static ulong        gc_growth_percent;
    static Context *    context;

private:
    Errors &            errors;
    active_set          active;
    active_set          roots;
    ulong               gc_threshold;
    Tree *              error_handler;
    compile_cache       compiled;
};


struct Rewrite
// ----------------------------------------------------------------------------
//   Information about a rewrite, e.g fact N -> N * fact(N-1) 
// ----------------------------------------------------------------------------
{
    Rewrite (Context *c, Tree *f, Tree *t):
        context(c), from(f), to(t), hash() {}
    ~Rewrite();

    Rewrite *           Add (Rewrite *rewrite);
    Tree *              Do(Action &a);
    Tree *              Compile(void);

public:
    Context *           context;
    Tree *              from;
    Tree *              to;
    rewrite_table       hash;
};

XL_END

#endif // CONTEXT_H
