//===--- SymbolYAML.cpp ------------------------------------------*- C++-*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "SymbolYAML.h"
#include "Index.h"
#include "Serialization.h"
#include "dex/DexIndex.h"
#include "llvm/ADT/Optional.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/Support/Errc.h"
#include "llvm/Support/MemoryBuffer.h"
#include "llvm/Support/raw_ostream.h"

LLVM_YAML_IS_DOCUMENT_LIST_VECTOR(clang::clangd::Symbol)
LLVM_YAML_IS_SEQUENCE_VECTOR(clang::clangd::Symbol::IncludeHeaderWithReferences)

namespace llvm {
namespace yaml {

using clang::clangd::Symbol;
using clang::clangd::SymbolID;
using clang::clangd::SymbolLocation;
using clang::index::SymbolInfo;
using clang::index::SymbolKind;
using clang::index::SymbolLanguage;

// Helper to (de)serialize the SymbolID. We serialize it as a hex string.
struct NormalizedSymbolID {
  NormalizedSymbolID(IO &) {}
  NormalizedSymbolID(IO &, const SymbolID &ID) {
    llvm::raw_string_ostream OS(HexString);
    OS << ID;
  }

  SymbolID denormalize(IO &) {
    SymbolID ID;
    HexString >> ID;
    return ID;
  }

  std::string HexString;
};

template <> struct MappingTraits<SymbolLocation::Position> {
  static void mapping(IO &IO, SymbolLocation::Position &Value) {
    IO.mapRequired("Line", Value.Line);
    IO.mapRequired("Column", Value.Column);
  }
};

template <> struct MappingTraits<SymbolLocation> {
  static void mapping(IO &IO, SymbolLocation &Value) {
    IO.mapRequired("FileURI", Value.FileURI);
    IO.mapRequired("Start", Value.Start);
    IO.mapRequired("End", Value.End);
  }
};

template <> struct MappingTraits<SymbolInfo> {
  static void mapping(IO &io, SymbolInfo &SymInfo) {
    // FIXME: expose other fields?
    io.mapRequired("Kind", SymInfo.Kind);
    io.mapRequired("Lang", SymInfo.Lang);
  }
};

template <>
struct MappingTraits<clang::clangd::Symbol::IncludeHeaderWithReferences> {
  static void mapping(IO &io,
                      clang::clangd::Symbol::IncludeHeaderWithReferences &Inc) {
    io.mapRequired("Header", Inc.IncludeHeader);
    io.mapRequired("References", Inc.References);
  }
};

template <> struct MappingTraits<Symbol> {
  static void mapping(IO &IO, Symbol &Sym) {
    MappingNormalization<NormalizedSymbolID, SymbolID> NSymbolID(IO, Sym.ID);
    IO.mapRequired("ID", NSymbolID->HexString);
    IO.mapRequired("Name", Sym.Name);
    IO.mapRequired("Scope", Sym.Scope);
    IO.mapRequired("SymInfo", Sym.SymInfo);
    IO.mapOptional("CanonicalDeclaration", Sym.CanonicalDeclaration,
                   SymbolLocation());
    IO.mapOptional("Definition", Sym.Definition, SymbolLocation());
    IO.mapOptional("References", Sym.References, 0u);
    IO.mapOptional("IsIndexedForCodeCompletion", Sym.IsIndexedForCodeCompletion,
                   false);
    IO.mapOptional("Signature", Sym.Signature);
    IO.mapOptional("CompletionSnippetSuffix", Sym.CompletionSnippetSuffix);
    IO.mapOptional("Documentation", Sym.Documentation);
    IO.mapOptional("ReturnType", Sym.ReturnType);
    IO.mapOptional("IncludeHeaders", Sym.IncludeHeaders);
  }
};

template <> struct ScalarEnumerationTraits<SymbolLanguage> {
  static void enumeration(IO &IO, SymbolLanguage &Value) {
    IO.enumCase(Value, "C", SymbolLanguage::C);
    IO.enumCase(Value, "Cpp", SymbolLanguage::CXX);
    IO.enumCase(Value, "ObjC", SymbolLanguage::ObjC);
    IO.enumCase(Value, "Swift", SymbolLanguage::Swift);
  }
};

template <> struct ScalarEnumerationTraits<SymbolKind> {
  static void enumeration(IO &IO, SymbolKind &Value) {
#define DEFINE_ENUM(name) IO.enumCase(Value, #name, SymbolKind::name)

    DEFINE_ENUM(Unknown);
    DEFINE_ENUM(Function);
    DEFINE_ENUM(Module);
    DEFINE_ENUM(Namespace);
    DEFINE_ENUM(NamespaceAlias);
    DEFINE_ENUM(Macro);
    DEFINE_ENUM(Enum);
    DEFINE_ENUM(Struct);
    DEFINE_ENUM(Class);
    DEFINE_ENUM(Protocol);
    DEFINE_ENUM(Extension);
    DEFINE_ENUM(Union);
    DEFINE_ENUM(TypeAlias);
    DEFINE_ENUM(Function);
    DEFINE_ENUM(Variable);
    DEFINE_ENUM(Field);
    DEFINE_ENUM(EnumConstant);
    DEFINE_ENUM(InstanceMethod);
    DEFINE_ENUM(ClassMethod);
    DEFINE_ENUM(StaticMethod);
    DEFINE_ENUM(InstanceProperty);
    DEFINE_ENUM(ClassProperty);
    DEFINE_ENUM(StaticProperty);
    DEFINE_ENUM(Constructor);
    DEFINE_ENUM(Destructor);
    DEFINE_ENUM(ConversionFunction);
    DEFINE_ENUM(Parameter);
    DEFINE_ENUM(Using);

#undef DEFINE_ENUM
  }
};

} // namespace yaml
} // namespace llvm

namespace clang {
namespace clangd {

SymbolSlab symbolsFromYAML(llvm::StringRef YAMLContent) {
  llvm::yaml::Input Yin(YAMLContent);
  std::vector<Symbol> S;
  Yin >> S;

  SymbolSlab::Builder Syms;
  for (auto &Sym : S)
    Syms.insert(Sym);
  return std::move(Syms).build();
}

Symbol SymbolFromYAML(llvm::yaml::Input &Input) {
  Symbol S;
  Input >> S;
  return S;
}

void SymbolsToYAML(const SymbolSlab &Symbols, llvm::raw_ostream &OS) {
  llvm::yaml::Output Yout(OS);
  for (Symbol S : Symbols) // copy: Yout<< requires mutability.
    Yout << S;
}

std::string SymbolToYAML(Symbol Sym) {
  std::string Str;
  llvm::raw_string_ostream OS(Str);
  llvm::yaml::Output Yout(OS);
  Yout << Sym;
  return OS.str();
}

std::unique_ptr<SymbolIndex> loadIndex(llvm::StringRef SymbolFile,
                                       bool UseDex) {
  auto Buffer = llvm::MemoryBuffer::getFile(SymbolFile);
  if (!Buffer) {
    llvm::errs() << "Can't open " << SymbolFile << "\n";
    return nullptr;
  }
  StringRef Data = Buffer->get()->getBuffer();

  llvm::Optional<SymbolSlab> Slab;
  if (Data.startswith("RIFF")) { // Magic for binary index file.
    if (auto RIFF = readIndexFile(Data))
      Slab = std::move(RIFF->Symbols);
    else
      llvm::errs() << "Bad RIFF: " << llvm::toString(RIFF.takeError()) << "\n";
  } else {
    Slab = symbolsFromYAML(Data);
  }

  return UseDex ? dex::DexIndex::build(std::move(*Slab))
                : MemIndex::build(std::move(*Slab), RefSlab());
}

} // namespace clangd
} // namespace clang
