// ****************************************************************************
//  xl.semantics.generics.xs        (C) 1992-2004 Christophe de Dinechin (ddd) 
//                                                                 XL2 project 
// ****************************************************************************
// 
//   File Description:
// 
//    Interface of generic manipulations in XL
// 
// 
// 
// 
// 
// 
// 
// 
// ****************************************************************************
// This document is released under the GNU General Public License.
// See http://www.gnu.org/copyleft/gpl.html and Matthew 25:22 for details
// ****************************************************************************
// * File       : $RCSFile$
// * Revision   : $Revision$
// * Date       : $Date$
// ****************************************************************************

import PT = XL.PARSER.TREE
import SYM = XL.SYMBOLS
import DCL = XL.SEMANTICS.DECLARATIONS
import TY = XL.SEMANTICS.TYPES
import GEN = XL.SEMANTICS.TYPES.GENERICS

module XL.SEMANTICS.GENERICS with
// ----------------------------------------------------------------------------
//    Module implementing manipulations of generics
// ----------------------------------------------------------------------------

    type generic_map is map[PT.tree, PT.tree]

    procedure LookupInstantiation(NameTree       : PT.tree;
                                  Args           : PT.tree;
                                  kind           : text;
                                  out BestDecl   : DCL.declaration;
                                  out ActualArgs : generic_map;
                                  out ScopeExpr  : PT.tree)

    function Instantiate(Decl       : DCL.declaration;
                         Args       : generic_map;
                         Variadics  : PT.tree;
                         BaseRecord : PT.tree) return BC.bytecode

    function InstantiateType (Name : PT.tree;
                              Args : PT.tree) return BC.bytecode
    function InstantiateType (Decl : DCL.declaration;
                              Args : generic_map) return BC.bytecode
    function InstantiateFunction (Source     : PT.tree;
                                  Decl       : DCL.declaration;
                                  Args       : generic_map;
                                  Variadics  : PT.tree;
                                  BaseRecord : PT.tree) return BC.bytecode
    procedure SetContextOfInstantiation (instContext : SYM.symbol_table;
                                         oldContext  : SYM.symbol_table)
    function ContextOfInstantiation () return SYM.symbol_table


    function IsGenericName(Name : PT.tree; kind : text) return boolean

    function Deduce (FunType      : GEN.generic_type;
                     Decl         : DCL.declaration;
                     Arg          : PT.tree;
                     in out Gargs : generic_map) return boolean

