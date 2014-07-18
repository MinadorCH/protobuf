// Protocol Buffers - Google's data interchange format
// Copyright 2008 Google Inc.  All rights reserved.
// http://code.google.com/p/protobuf/
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are
// met:
//
//     * Redistributions of source code must retain the above copyright
// notice, this list of conditions and the following disclaimer.
//     * Redistributions in binary form must reproduce the above
// copyright notice, this list of conditions and the following disclaimer
// in the documentation and/or other materials provided with the
// distribution.
//     * Neither the name of Google Inc. nor the names of its
// contributors may be used to endorse or promote products derived from
// this software without specific prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
// "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
// LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
// A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
// OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
// SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
// LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
// DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
// THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
// (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
// OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

// Author: xiaofeng@google.com (Feng Xiao)

#include <google/protobuf/compiler/java/java_shared_code_generator.h>

#include <memory>

#include <google/protobuf/compiler/java/java_helpers.h>
#include <google/protobuf/compiler/java/java_name_resolver.h>
#include <google/protobuf/compiler/code_generator.h>
#include <google/protobuf/io/printer.h>
#include <google/protobuf/io/zero_copy_stream.h>
#include <google/protobuf/descriptor.pb.h>
#include <google/protobuf/descriptor.h>
#include <google/protobuf/stubs/strutil.h>

namespace google {
namespace protobuf {
namespace compiler {
namespace java {

SharedCodeGenerator::SharedCodeGenerator(const FileDescriptor* file)
  : name_resolver_(new ClassNameResolver), file_(file) {
}

SharedCodeGenerator::~SharedCodeGenerator() {
}

void SharedCodeGenerator::Generate(GeneratorContext* context,
                                   vector<string>* file_list) {
  string java_package = FileJavaPackage(file_);
  string package_dir = JavaPackageToDir(java_package);

  if (HasDescriptorMethods(file_)) {
    // Generate descriptors.
    string classname = name_resolver_->GetDescriptorClassName(file_);
    string filename = package_dir + classname + ".java";
    file_list->push_back(filename);
    scoped_ptr<io::ZeroCopyOutputStream> output(context->Open(filename));
    scoped_ptr<io::Printer> printer(new io::Printer(output.get(), '$'));

    printer->Print(
      "// Generated by the protocol buffer compiler.  DO NOT EDIT!\n"
      "// source: $filename$\n"
      "\n",
      "filename", file_->name());
    if (!java_package.empty()) {
      printer->Print(
        "package $package$;\n"
        "\n",
        "package", java_package);
    }
    printer->Print(
      "public final class $classname$ {\n",
      "classname", classname);
    printer->Indent();
    GenerateDescriptors(printer.get());
    printer->Outdent();
    printer->Print(
      "}\n");

    printer.reset();
    output.reset();
  }
}


void SharedCodeGenerator::GenerateDescriptors(io::Printer* printer) {
  // Embed the descriptor.  We simply serialize the entire FileDescriptorProto
  // and embed it as a string literal, which is parsed and built into real
  // descriptors at initialization time.  We unfortunately have to put it in
  // a string literal, not a byte array, because apparently using a literal
  // byte array causes the Java compiler to generate *instructions* to
  // initialize each and every byte of the array, e.g. as if you typed:
  //   b[0] = 123; b[1] = 456; b[2] = 789;
  // This makes huge bytecode files and can easily hit the compiler's internal
  // code size limits (error "code to large").  String literals are apparently
  // embedded raw, which is what we want.
  FileDescriptorProto file_proto;
  file_->CopyTo(&file_proto);


  string file_data;
  file_proto.SerializeToString(&file_data);

  printer->Print(
    "public static com.google.protobuf.Descriptors.FileDescriptor\n"
    "    descriptor;\n"
    "static {\n"
    "  java.lang.String[] descriptorData = {\n");
  printer->Indent();
  printer->Indent();

  // Only write 40 bytes per line.
  static const int kBytesPerLine = 40;
  for (int i = 0; i < file_data.size(); i += kBytesPerLine) {
    if (i > 0) {
      // Every 400 lines, start a new string literal, in order to avoid the
      // 64k length limit.
      if (i % 400 == 0) {
        printer->Print(",\n");
      } else {
        printer->Print(" +\n");
      }
    }
    printer->Print("\"$data$\"",
      "data", CEscape(file_data.substr(i, kBytesPerLine)));
  }

  printer->Outdent();
  printer->Print("\n};\n");

  // -----------------------------------------------------------------
  // Create the InternalDescriptorAssigner.

  printer->Print(
    "com.google.protobuf.Descriptors.FileDescriptor."
    "InternalDescriptorAssigner assigner =\n"
    "    new com.google.protobuf.Descriptors.FileDescriptor."
    "    InternalDescriptorAssigner() {\n"
    "      public com.google.protobuf.ExtensionRegistry assignDescriptors(\n"
    "          com.google.protobuf.Descriptors.FileDescriptor root) {\n"
    "        descriptor = root;\n"
    // Custom options will be handled when immutable messages' outer class is
    // loaded. Here we just return null and let custom options be unknown
    // fields.
    "        return null;\n"
    "      }\n"
    "    };\n");

  // -----------------------------------------------------------------
  // Find out all dependencies.
  vector<pair<string, string> > dependencies;
  for (int i = 0; i < file_->dependency_count(); i++) {
    if (ShouldIncludeDependency(file_->dependency(i))) {
      string filename = file_->dependency(i)->name();
      string classname = FileJavaPackage(file_->dependency(i)) + "." +
                         name_resolver_->GetDescriptorClassName(
                             file_->dependency(i));
      dependencies.push_back(make_pair(filename, classname));
    }
  }

  // -----------------------------------------------------------------
  // Invoke internalBuildGeneratedFileFrom() to build the file.
  printer->Print(
    "com.google.protobuf.Descriptors.FileDescriptor\n"
    "  .internalBuildGeneratedFileFrom(descriptorData,\n");

  printer->Print(
    "    $classname$.class,\n"
    "    new java.lang.String[] {\n",
    "classname", name_resolver_->GetDescriptorClassName(file_));
  for (int i = 0; i < dependencies.size(); i++) {
    const string& dependency = dependencies[i].second;
    printer->Print(
        // Here we load the dependency FileDescriptors lazily via Java
        // reflection. This is to avoid breaking proto1 targets who have
        // genproto dependencies for which we can't generate the descriptor
        // class. They will compile fine but when users try to call reflection
        // functions upon them it will fail. Users will have to get rid of
        // genproto dependencies before they can use proto2 reflection on
        // proto1 messages.
        "      \"$dependency$\",\n",
        "dependency", dependency);
  }

  printer->Print(
      "    }, new java.lang.String[] {\n");

  for (int i = 0; i < dependencies.size(); i++) {
    const string& filename = dependencies[i].first;
    printer->Print(
        "      \"$filename$\",\n",
        "filename", filename);
  }

  printer->Print(
    "    }, assigner);\n");

  printer->Outdent();
  printer->Print(
    "}\n");
}

bool SharedCodeGenerator::ShouldIncludeDependency(
    const FileDescriptor* descriptor) {
  return true;
}

}  // namespace java
}  // namespace compiler
}  // namespace protobuf
}  // namespace google
