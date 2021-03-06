(**
 * Copyright (c) 2015, Facebook, Inc.
 * All rights reserved.
 *
 * This source code is licensed under the BSD-style license found in the
 * LICENSE file in the "hack" directory of this source tree. An additional grant
 * of patent rights can be found in the PATENTS file in the same directory.
 *
 *)

open Sys_utils
module List = Core_list
module Sys = Sys_ext
module CE = Common_exns

include Parser_hack

let parse_or_die file =
  let content = cat (Relative_path.to_absolute file) in
  let errors, res, _ = Errors.do_
    (fun () -> Parser_hack.program_with_default_popt
      ~elaborate_namespaces:false
      file
      content) in
  if not (Errors.is_empty errors)
  then raise (CE.ParseErrors (Errors.get_error_list errors));
  res
