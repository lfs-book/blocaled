/*
  Copyright 2012 Alexandre Rostovtsev

  Permission is hereby granted, free of charge, to any person obtaining
  a copy of this software and associated documentation files (the
  "Software"), to deal in the Software without restriction, including
  without limitation the rights to use, copy, modify, merge, publish,
  distribute, sublicense, and/or sell copies of the Software, and to
  permit persons to whom the Software is furnished to do so, subject to
  the following conditions:

  The above copyright notice and this permission notice shall be
  included in all copies or substantial portions of the Software.

  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
  EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
  MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
  IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY
  CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
  TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
  SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
*/

#ifndef _MAIN_H_
#define _MAIN_H_

/**
 * SECTION: main
 * @title: Main program
 * @short_description: blocaled daemon launcher
 * @see_also: #localed
 *
 * This program forks (except if run with --foreground option) and
 * runs #localed_init to connect to the message bus. It returns an error
 * if localed_init does not call back #localed_component_started within
 * 20 s. #localed should call #localed_exit when done (either by normal
 * exit or on error)
 */

void
localed_component_started ();

void
localed_exit (int status);

#endif
