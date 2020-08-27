// -*- mode:C++; tab-width:8; c-basic-offset:2; indent-tabs-mode:t -*-
// vim: ts=8 sw=2 smarttab

#include <random>

#include "CDC.h"
#include "FastCDC.h"
#include "FixedCDC.h"

std::unique_ptr<CDC> CDC::create(
  const std::string& type,
  int bits,
  int windowbits)
{
  if (type == "fastcdc") {
    return std::unique_ptr<CDC>(new FastCDC(bits, windowbits));
  }
  if (type == "fixed") {
    return std::unique_ptr<CDC>(new FixedCDC(bits, windowbits));
  }
  return nullptr;
}
