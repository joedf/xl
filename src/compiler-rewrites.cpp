// *****************************************************************************
// compiler-rewrites.cpp                                              XL project
// *****************************************************************************
//
// File description:
//
//    Check if a tree matches the pattern on the left of a rewrite
//
//
//
//
//
//
//
//
// *****************************************************************************
// This software is licensed under the GNU General Public License v3
// (C) 2003-2004,2006,2010-2019, Christophe de Dinechin <christophe@dinechin.org>
// (C) 2012, Jérôme Forissier <jerome@taodyne.com>
// *****************************************************************************
// This file is part of XL
//
// XL is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// XL is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with XL, in a file named COPYING.
// If not, see <https://www.gnu.org/licenses/>.
// *****************************************************************************

#include "compiler-rewrites.h"
#include "compiler-unit.h"
#include "compiler-function.h"
#include "compiler-types.h"
#include "compiler.h"
#include "save.h"
#include "errors.h"
#include "renderer.h"
#include "main.h"
#include "basics.h"


RECORDER(call_types, 64, "Type information in calls");
RECORDER(argument_bindings, 64, "Binding arguments in calls");


XL_BEGIN

bool RewriteBinding::IsDeferred()
// ----------------------------------------------------------------------------
//   Return true if we want to defer evaluation for the given value
// ----------------------------------------------------------------------------
// We defer evaluation for indent and {} blocks, sequences and functions
{
    Tree *val = value;
    if (Block *block = val->AsBlock())
    {
        // Defer evaluation of indent and {} blocks
        if (block->IsIndent() || block->IsBraces())
            return true;

        // If we have a block with a deferred child, defer
        if (Infix *infix = block->child->AsInfix())
            val = infix;
    }

    // Defer sequences and function definitions
    if (Infix *infix = val->AsInfix())
        return IsSequence(infix) || IsDefinition(infix);

    return false;
}


RewriteCandidate::RewriteCandidate(Infix *rewrite, Scope *scope, Types *types)
// ----------------------------------------------------------------------------
//   Create a rewrite candidate within the given types
// ----------------------------------------------------------------------------
    : rewrite(rewrite),
      scope(scope),
      bindings(),
      value_types(types),
      binding_types(new Types(scope, types)),
      type(nullptr),
      defined(nullptr),
      defined_name()
{}


Tree *RewriteCandidate::ValueType(Tree *value)
// ----------------------------------------------------------------------------
//   Return the value type for this value, and associated calls
// ----------------------------------------------------------------------------
{
    Tree *vtype = value_types->Type(value);
    if (vtype)
    {
        while (value)
        {
            if (RewriteCalls *calls = value_types->HasRewriteCalls(value))
            {
                rcall_map &bcalls = binding_types->TypesRewriteCalls();
                bcalls[value] = calls;
            }
            if (Block *block = value->AsBlock())
                value = block->child;
            else
                value = nullptr;
        }
    }
    return vtype;
}


BindingStrength RewriteCandidate::Bind(Tree *pattern,
                                       Tree *value)
// ----------------------------------------------------------------------------
//   Attempts to bind 'value' to the given 'pattern'
// ----------------------------------------------------------------------------
{
    if (pattern == value)
        return PERFECT;

    static const char *sname[] = { "impossible", "possible", "unconditional" };
    Tree *vtype = nullptr;
    kind k = pattern->Kind();

    switch(k)
    {
#define BIND_CONSTANT(Type, mtype)                                      \
        {                                                               \
            Type *f = (Type *) pattern;                                 \
            if (Type *iv = value->As##Type())                           \
            {                                                           \
                BindingStrength result =                                \
                    iv->value == f->value ? PERFECT : FAILED;           \
                record(argument_bindings,                               \
                       "Binding " #mtype " constant %t to %t in %p "    \
                       "is %+s",                                        \
                       pattern, value, this, sname[result]);            \
                return result;                                          \
            }                                                           \
            vtype = ValueType(value);                                   \
            if (Unify(vtype, mtype##_type, value, pattern))             \
            {                                                           \
                Condition(value, pattern);                              \
                record(argument_bindings,                               \
                       "Binding " #mtype " %t to %t in %p is possible", \
                       pattern, value, this);                           \
                return POSSIBLE;                                        \
            }                                                           \
            record(argument_bindings,                                   \
                   "Binding " #mtype " %t to %t in %p type mismatch",   \
                   pattern, value, this);                               \
            return FAILED;                                              \
        }


    case INTEGER:       BIND_CONSTANT(Integer, integer)
    case REAL:          BIND_CONSTANT(Real, real);
    case TEXT:          BIND_CONSTANT(Text, text);

    case NAME:
    {
        Name *name = (Name *) pattern;
        bool needArg = true;

        // Ignore function name if that is all we have
        Tree *fname = PatternBase(rewrite->left);
        if (fname == name)
        {
            defined = name;
            defined_name = name->value;
            record(argument_bindings,
                   "Binding identical name %t to %t in %p is unconditional",
                   pattern, value, this);
            return PERFECT;     // Will degrade to 'POSSIBLE' if there are args
        }

        // Check if what we have as an expression evaluates correctly
        vtype = ValueType(value);
        if (!vtype)
        {
            record(argument_bindings,
                   "Binding identical name %t to %t in %p type mismatch",
                   pattern, value, this);
            return FAILED;
        }

        // Test if the name is already bound, and if so, if trees fail to match
        Context *context = binding_types->TypesContext();
        if (Tree *bound = context->DeclaredPattern(name))
        {
            if (bound != name)
            {
                Tree *boundType = ValueType(bound);
                if (!Unify(vtype, boundType, value, pattern))
                {
                    record(argument_bindings,
                           "Binding duplicate name %t to %t in %p "
                           "type mismatch",
                           pattern, value, this);
                    return FAILED;
                }

                // We need to have the same value
                record(argument_bindings,
                       "Binding duplicate name %t to %t in %p "
                       "check values",
                       pattern, value, this);

                Condition(value, pattern);

                // Since we are testing an existing value, don't pass arg
                needArg = false;
            }
        }

        // Check if we can unify the value and name types
        Tree *nameType = binding_types->DeclarationType(name);
        if (!Unify(vtype, nameType, value, pattern))
        {
            record(argument_bindings,
                   "Binding name %t to %t in %p type mismatch",
                   pattern, value, this);
            return FAILED;
        }

        // Enter the name in the context and in the bindings
        if (needArg)
        {
            record(argument_bindings,
                   "Binding name %t to %t in %p context %p",
                   pattern, value, this, (Context *) context);
            context->Define(pattern, value, true);
            bindings.push_back(RewriteBinding(name, value));
        }
        else
        {
            record(argument_bindings,
                   "Binding name %t to %t in %p has no separate argument",
                   pattern, value, this);
        }
        return PERFECT;
    }

    case INFIX:
    {
        Infix *fi = (Infix *) pattern;

        // Check type declarations
        if (IsTypeAnnotation(fi))
        {
            // Assign the given type to the declared expression
            Tree *pattern = fi->left;
            Tree *declType = fi->right;
            vtype = binding_types->AssignType(pattern, declType);

            // Check if we can bind the value from what we know
            if (Bind(pattern, value) == FAILED)
            {
                record(argument_bindings,
                       "Binding name of typed %t to %t in %p failed",
                       pattern, value, this);
                return FAILED;
            }

            // Add type binding with the given type
            Tree *valueType = binding_types->Type(value);
            if (!Unify(valueType, vtype, value, pattern, true))
            {
                record(argument_bindings,
                       "Binding typed %t to %t in %p type mismatch",
                       pattern, value, this);
                return FAILED;
            }

            // Having been successful makes it a strong binding
            BindingStrength result = Unconditional() ? PERFECT : POSSIBLE;
            record(argument_bindings,
                   "Binding typed %t to %t in %p %+s",
                   pattern, value, this, sname[result]);
            return result;

        } // We have an infix :
        else if (fi->name == "when")
        {
            // We have a guard - first test if we can bind the left part
            if (Bind(fi->left, value) == FAILED)
            {
                record(argument_bindings,
                       "Binding name of conditional %t to %t in %p failed",
                       pattern, value, this);
                return FAILED;
            }

            // Check if we can evaluate the guard
            if (!binding_types->Type(fi->right))
            {
                record(argument_bindings,
                       "Guard of conditional %t to %t in %p type mismatch",
                       pattern, value, this);
                return FAILED;
            }

            // Check that the type of the guard is a boolean
            Tree *guardType = binding_types->Type(fi->right);
            if (!Unify(guardType, boolean_type, fi->right, fi->left))
            {
                record(argument_bindings,
                       "Binding conditional %t to %t in %p type mismatch",
                       pattern, value, this);
                return FAILED;
            }

            // Add the guard condition
            Condition(fi->right, xl_true);

            // The guard makes the binding weak
            record(argument_bindings,
                   "Binding conditional %t to %t in %p added condition",
                   pattern, value, this);
            return POSSIBLE;
        }

        // Check if this infix is what we are defining
        if (!defined)
        {
            defined = fi;
            defined_name = "infix[" + fi->name + "]";
        }

        // If we match the infix name, we can bind left and right
        if (Infix *infix = value->AsInfix())
        {
            if (fi->name == infix->name)
            {
                BindingStrength left = Bind(fi->left, infix->left);
                if (left == FAILED)
                    return FAILED;
                BindingStrength right = Bind(fi->right, infix->right);

                // Return the weakest binding
                if (left > right)
                    left = right;

                record(argument_bindings,
                       "Binding infix %t to %t in %p is %+s",
                       pattern, value, this, sname[left]);
                return left;
            }
        }

        // We may have an expression that evaluates as an infix

        // Check if what we have as an expression evaluates correctly
        vtype = binding_types->Type(value);
        if (!vtype)
        {
            record(argument_bindings,
                   "Binding infix %t to %t in %p value type mismatch",
                   pattern, value, this);
            return FAILED;
        }

        // Then check if the type matches
        if (!Unify(vtype, infix_type, value, pattern))
        {
            record(argument_bindings,
                   "Binding infix %t to %t in %p type mismatch",
                   pattern, value, this);
            return FAILED;
        }

        // If we had to evaluate, we need a runtime pattern match (weak binding)
        TreePosition pos = pattern->Position();
        Tree *infixLeft  = new Prefix(new Name("left", pos), value);
        BindingStrength left  = Bind(fi->left, infixLeft);
        if (left == FAILED)
            return FAILED;
        Tree *infixRight = new Prefix(new Name("right", pos), value);
        BindingStrength right = Bind(fi->right, infixRight);

        // Add a condition on the infix name
        Tree *infixName = new Prefix(new Name("name", pos), value);
        if (!value_types->Type(infixName))
        {
            record(argument_bindings,
                   "Binding infix %t to %t in %p name mismatch",
                   pattern, value, this);
            return FAILED;
        }
        Tree *infixRequiredName = new Text(fi->name, pos);
        if (!binding_types->Type(infixRequiredName))
        {
            record(argument_bindings,
                   "Binding infix %t to %t in %p text mismatch",
                   pattern, value, this);
            return FAILED;
        }
        Condition(infixName, infixRequiredName);

        // Return weakest binding
        if (left > right)
            left = right;

        record(argument_bindings,
               "Binding infix %t to %t in %p is %+s",
               pattern, value, this, sname[left]);
        return left;
    }

    case PREFIX:
    {
        Prefix *prefixPattern = (Prefix *) pattern;

        // Must match a postfix with the same name
        BindingStrength ok = FAILED;
        if (Prefix *prefixValue = value->AsPrefix())
        {
            ok = BindBinary(prefixPattern->left,
                            prefixValue->left,
                            prefixPattern->right,
                            prefixValue->right);
        }
        record(argument_bindings,
               "Binding prefix %t to %t in %p is %+s",
               pattern, value, this, sname[ok]);
        return ok;
    }

    case POSTFIX:
    {
        Postfix *postfixPattern = (Postfix *) pattern;

        // Must match a postfix with the same name
        // REVISIT: Variables that denote a function name...
        BindingStrength ok = FAILED;
        if (Postfix *postfixValue = value->AsPostfix())
        {
            ok = BindBinary(postfixPattern->right,
                            postfixValue->right,
                            postfixPattern->left,
                            postfixValue->left);
        }
        record(argument_bindings,
               "Binding postfix %t to %t in %p is %+s",
               pattern, value, this, sname[ok]);
        return ok;
    }

    case BLOCK:
    {
        // Ignore blocks, just look inside
        Block *block = (Block *) pattern;
        BindingStrength ok = Bind(block->child, value);
        record(argument_bindings, "Binding block %t to %t in %p is %+s",
               pattern, value, this, sname[ok]);
        return ok;
    }
    }

    // Default is to return false
    record(argument_bindings,
           "Binding %t to %t in %p: unexpected kind %u",
           pattern, value, this, k);
    return FAILED;
}


BindingStrength RewriteCandidate::BindBinary(Tree *pattern1, Tree *value1,
                                             Tree *pattern2, Tree *value2)
// ----------------------------------------------------------------------------
//    Bind a binary pattern (prefix or postfix)
// ----------------------------------------------------------------------------
{
    // Check if we have the same name as operand, e.g 'sin X' vs 'sin (A+B)'
    Name *patternName = pattern1->AsName();
    if (!patternName)
        return FAILED;
    Name *valueName = value1->AsName();
    if (!valueName)
        return FAILED;
    if (patternName->value != valueName->value)
        return FAILED;
    if (!defined)
    {
        defined = patternName;
        defined_name = "xl." + patternName->value;
    }


    return Bind(pattern2, value2);
}


bool RewriteCandidate::Unify(Tree *valueType, Tree *patternType,
                             Tree *value, Tree *pattern,
                             bool declaration)
// ----------------------------------------------------------------------------
//   Check unification for types in a given candidate
// ----------------------------------------------------------------------------
{
    Tree *refType = binding_types->DeclaredTypeName(valueType);

    record(call_types,
           "Unify %t as %t with %t as %t", value, valueType, pattern, patternType);

    // If we have a tree, it may have the right type, must check at runtime
    if (refType == tree_type)
    {
        Tree *vrefType = binding_types->DeclaredTypeName(patternType);
        kind k = valueType->Kind();
        if (k == INTEGER || vrefType == integer_type)
            KindCondition(value, INTEGER);
        else if (k == REAL || vrefType == real_type)
            KindCondition(value, REAL);
        else if (k == TEXT || vrefType == text_type)
            KindCondition(value, TEXT);
        else if (vrefType == name_type || vrefType == boolean_type)
            KindCondition(value, NAME);
        else if (vrefType == block_type)
            KindCondition(value, BLOCK);
        else if (k == INFIX || vrefType == infix_type)
            KindCondition(value, INFIX);
        else if (vrefType == prefix_type)
            KindCondition(value, PREFIX);
        else if (vrefType == postfix_type)
            KindCondition(value, POSTFIX);
    }

    // Otherwise, do type inference
    Tree *unified = binding_types->Unify(valueType, patternType);
    unified = binding_types->AssignType(value, unified);
    unified = binding_types->AssignType(pattern, unified);
    return unified;
}


JIT::Function_p RewriteCandidate::Prototype(JIT &jit)
// ----------------------------------------------------------------------------
//   Build the prototype for the rewrite function
// ----------------------------------------------------------------------------
{
    JIT::FunctionType_p fty = FunctionType(jit);
    text fname = FunctionName();
    JIT::Function_p function = jit.Function(fty, fname);
    return function;
}


JIT::FunctionType_p RewriteCandidate::FunctionType(JIT &jit)
// ----------------------------------------------------------------------------
//   Build the signature type for the function
// ----------------------------------------------------------------------------
{
    JIT::Signature signature = RewriteSignature();
    JIT::Type_p retTy = RewriteType();
    return jit.FunctionType(retTy, signature);
}


text RewriteCandidate::FunctionName()
// ----------------------------------------------------------------------------
//   Return the signature name for the given rewrite candidate
// ----------------------------------------------------------------------------
{
    return defined_name;
}


JIT::Signature RewriteCandidate::RewriteSignature()
// ----------------------------------------------------------------------------
//   Build the signature for the rewrite
// ----------------------------------------------------------------------------
{
    JIT::Signature signature;
    for (RewriteBinding &binding : bindings)
    {
        Tree *valueType = ValueType(binding.value);
        assert(valueType && "Type for bound value is required during codegen");
        JIT::Type_p valueTy = value_types->BoxedType(valueType);
        assert(valueTy && "Machine type for bound value should exist");
        signature.push_back(valueTy);
    }
    return signature;
}


JIT::Type_p RewriteCandidate::RewriteType()
// ----------------------------------------------------------------------------
//   Boxed type for the rewrite
// ----------------------------------------------------------------------------
{
    JIT::Type_p ty = binding_types->BoxedType(type);
    return ty;
}


void RewriteCandidate::RewriteType(JIT::Type_p ty)
// ----------------------------------------------------------------------------
//   Set the boxed type for the rewrite
// ----------------------------------------------------------------------------
{
    binding_types->AddBoxedType(type, ty);
}


void RewriteCandidate::Dump()
// ----------------------------------------------------------------------------
//   Dump a rewrite candidate
// ----------------------------------------------------------------------------
{
    std::cout << "\t" << rewrite->left
              << "\t: " << type << "\n";

    for (auto &t : conditions)
        std::cout << "\t\tWhen " << ShortTreeForm(t.value)
                  << "\t= " << ShortTreeForm(t.test) << "\n";

    for (auto &b : bindings)
    {
        std::cout << "\t\t" << b.name << " (" << (void *) b.name << ") ";
        std::cout << "\t= "
                  << ShortTreeForm(b.value)
                  << " (" << (void *) b.value << ")\n";
    }
}



// ============================================================================
//
//   Rewrite Calls
//
// ============================================================================

RewriteCalls::RewriteCalls(Types *types)
// ----------------------------------------------------------------------------
//   Create a new type context to evaluate the calls for a rewrite
// ----------------------------------------------------------------------------
    : types(types),
      candidates()
{}


Tree *RewriteCalls::Check (Scope *scope,
                           Tree *what,
                           Infix *candidate)
// ----------------------------------------------------------------------------
//   Check which candidates match, and what binding is required to match
// ----------------------------------------------------------------------------
{
    Errors errors;
    errors.Log(Error("Pattern $1 doesn't match:", candidate->left), true);

    // Create local type inference deriving from ours
    RewriteCandidate *rc = new RewriteCandidate(candidate, scope, types);

    // All the following is in candidate types
    Types *binding_types = rc->binding_types;
    record(types, "Types %p created for bindings of %t in candidate %t",
           binding_types, what, candidate->left);

    // Attempt binding / unification of parameters to arguments
    Tree *pattern = candidate->left;
    Tree *defined = PatternBase(pattern);
    Tree *declType = AnnotatedType(pattern);
    Tree *type = declType
        ? types->EvaluateType(declType)
        : types->KnownType(pattern);

    BindingStrength binding = rc->Bind(defined, what);
    if (binding == FAILED)
        return nullptr;

    // If argument/parameters binding worked, try to typecheck the definition
    Tree *init = candidate->right;

    bool builtin = false;
    if (init)
    {
        // Check if we have a type to match
        if (type)
        {
            type = binding_types->AssignType(init, type);
            type = binding_types->AssignType(what, type);
            if (!type)
                binding = FAILED;
        }

        // Check built-ins and C functions
        if (binding != FAILED)
        {
            builtin = Types::RewriteCategory(rc) != Types::Decl::NORMAL;
            if (!builtin)
            {
                // Process declarations in the initializer
                Context *bcontext = binding_types->TypesContext();
                bcontext->ProcessDeclarations(init);
                type = binding_types->Type(init);
                if (!type)
                    binding = FAILED;
            }
            else if (!declType)
            {
                // No type specified, assign a generic type
                type = binding_types->Type(init);
            }
        }
    }

    // Match the type of the pattern and declared entity
    if (binding != FAILED && type != nullptr)
    {
        type = binding_types->AssignType(pattern, type);
        if (defined && pattern != defined)
            type = binding_types->AssignType(defined, type);
    }

    // If we had some errors in the process, binding fails,
    // and we report errors back up, as this may be a bad unification
    if (errors.HadErrors())
        binding = FAILED;

    // If everything went well, define the type for the expression
    if (binding != FAILED)
    {
        type = binding_types->AssignType(what, type);
        if (!type)
            binding = FAILED;
    }

    // Record the rewrite candidate if we had any success with binding
    if (binding != FAILED)
    {
        // Record the type for that specific expression
        rc->type = type;
        candidates.push_back(rc);
    }

    // Keep going unless we had a perfect binding
    if (binding == PERFECT)
        return what;
    return nullptr;
}


void RewriteCalls::Dump()
// ----------------------------------------------------------------------------
//   Dump the rewrite calls for debugging purpose
// ----------------------------------------------------------------------------
{
    uint j = 0;
    for (RewriteCandidate *r : candidates)
    {
        std::cout << "\t#" << ++j;
        r->Dump();
    }
}

XL_END


XL::RewriteCalls *xldebug(XL::RewriteCalls *rc)
// ----------------------------------------------------------------------------
//   Debug rewrite calls
// ----------------------------------------------------------------------------
{
    if (!XL::Allocator<XL::RewriteCalls>::IsAllocated(rc))
        std::cout << "Cowardly refusing to show bad RewriteCalls pointer "
                  << (void *) rc << "\n";
    else
        rc->Dump();
    return rc;

}


XL::RewriteCalls *xldebug(XL::RewriteCalls_p rc)
// ----------------------------------------------------------------------------
//   Debug the GCPtr version
// ----------------------------------------------------------------------------
{
    return xldebug((XL::RewriteCalls *) rc);
}


XL::RewriteCandidate *xldebug(XL::RewriteCandidate *rc)
// ----------------------------------------------------------------------------
//   Debug rewrite calls
// ----------------------------------------------------------------------------
{
    if (!XL::Allocator<XL::RewriteCandidate>::IsAllocated(rc))
        std::cout << "Cowardly refusing to show bad RewriteCandidate pointer "
                  << (void *) rc << "\n";
    else
        rc->Dump();
    return rc;

}


XL::RewriteCandidate *xldebug(XL::RewriteCandidate_p rc)
// ----------------------------------------------------------------------------
//   Debug the GCPtr version
// ----------------------------------------------------------------------------
{
    return xldebug((XL::RewriteCandidate *) rc);
}
