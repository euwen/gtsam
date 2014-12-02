/* ---------------------------------------------------------------------------- 
 
 * GTSAM Copyright 2010, Georgia Tech Research Corporation,  
 * Atlanta, Georgia 30332-0415 
 * All Rights Reserved 
 * Authors: Frank Dellaert, et al. (see THANKS for the full author list) 
 
 * See LICENSE for the license information 
 
 * -------------------------------------------------------------------------- */ 
 
/** 
 * @file Module.ccp 
 * @author Frank Dellaert 
 * @author Alex Cunningham 
 * @author Andrew Melim 
 * @author Richard Roberts
 **/ 
 
#include "Module.h" 
#include "FileWriter.h" 
#include "TypeAttributesTable.h" 
#include "utilities.h"

//#define BOOST_SPIRIT_DEBUG
#include "spirit.h"
 
#ifdef __GNUC__
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-variable"
#endif
#include <boost/lambda/bind.hpp> 
#include <boost/lambda/lambda.hpp> 
#ifdef __GNUC__
#pragma GCC diagnostic pop
#endif
#include <boost/lambda/construct.hpp> 
#include <boost/foreach.hpp> 
#include <boost/filesystem.hpp> 
#include <boost/lexical_cast.hpp> 
 
#include <iostream> 
#include <algorithm> 

using namespace std; 
using namespace wrap; 
using namespace BOOST_SPIRIT_CLASSIC_NS; 
namespace bl = boost::lambda; 
namespace fs = boost::filesystem; 
 
/* ************************************************************************* */ 
// We parse an interface file into a Module object. 
// The grammar is defined using the boost/spirit combinatorial parser. 
// For example, str_p("const") parses the string "const", and the >> 
// operator creates a sequence parser. The grammar below, composed of rules 
// and with start rule [class_p], doubles as the specs for our interface files. 
/* ************************************************************************* */ 
 
/* ************************************************************************* */ 
// If a number of template arguments were given, generate a number of expanded
// class names, e.g., PriorFactor -> PriorFactorPose2, and add those classes
static void handle_possible_template(vector<Class>& classes, const Class& cls,
    const vector<Qualified>& instantiations) {
  if (cls.templateArgs.empty() || instantiations.empty()) {
    classes.push_back(cls);
  } else {
    if (cls.templateArgs.size() != 1)
      throw std::runtime_error(
          "In-line template instantiations only handle a single template argument");
    vector<Class> classInstantiations = //
        cls.expandTemplate(cls.templateArgs.front(), instantiations);
    BOOST_FOREACH(const Class& c, classInstantiations)
      classes.push_back(c);
  }
}

/* ************************************************************************* */
Module::Module(const std::string& moduleName, bool enable_verbose)
: name(moduleName), verbose(enable_verbose)
{
}

/* ************************************************************************* */ 
Module::Module(const string& interfacePath, 
         const string& moduleName, bool enable_verbose)
: name(moduleName), verbose(enable_verbose)
{ 
  // read interface file
  string interfaceFile = interfacePath + "/" + moduleName + ".h";
  string contents = file_contents(interfaceFile);

  // execute parsing
  parseMarkup(contents);
}

/* ************************************************************************* */
void Module::parseMarkup(const std::string& data) {
  // The parse imperatively :-( updates variables gradually during parse
  // The one with postfix 0 are used to reset the variables after parse. 
 
  //---------------------------------------------------------------------------- 
  // Grammar with actions that build the Class object. Actions are 
  // defined within the square brackets [] and are executed whenever a 
  // rule is successfully parsed. Define BOOST_SPIRIT_DEBUG to debug. 
  // The grammar is allows a very restricted C++ header 
  // lexeme_d turns off white space skipping 
  // http://www.boost.org/doc/libs/1_37_0/libs/spirit/classic/doc/directives.html 
  // ---------------------------------------------------------------------------- 
 
  // Define Rule and instantiate basic rules
  typedef rule<phrase_scanner_t> Rule;
  BasicRules<phrase_scanner_t> basic;

  // TODO, do we really need cls here? Non-local
  Class cls0(verbose),cls(verbose);
 
  // parse "gtsam::Pose2" and add to singleInstantiation.typeList
  TemplateInstantiationTypedef singleInstantiation, singleInstantiation0;
  TypeListGrammar<'<','>'> typelist_g(singleInstantiation.typeList);
 
  // typedef gtsam::RangeFactor<gtsam::Pose2, gtsam::Point2> RangeFactorPosePoint2;
  vector<string> namespaces; // current namespace tag
  TypeGrammar instantiationClass_g(singleInstantiation.class_);
  Rule templateSingleInstantiation_p = 
    (str_p("typedef") >> instantiationClass_g >>
    typelist_g >>
    basic.className_p[assign_a(singleInstantiation.name_)] >>
    ';') 
    [assign_a(singleInstantiation.namespaces_, namespaces)]
    [push_back_a(templateInstantiationTypedefs, singleInstantiation)] 
    [assign_a(singleInstantiation, singleInstantiation0)]; 
 
  // template<POSE, POINT>
  Rule templateList_p = 
    (str_p("template") >> 
    '<' >> basic.name_p[push_back_a(cls.templateArgs)] >> *(',' >> basic.name_p[push_back_a(cls.templateArgs)]) >>
    '>'); 

  // NOTE: allows for pointers to all types
  ArgumentList args;
  ArgumentListGrammar argumentList_g(args);

  // parse class constructor
  Constructor constructor0(verbose), constructor(verbose);
  Rule constructor_p =  
    (basic.className_p >> argumentList_g >> ';' >> !basic.comments_p)
    [bl::bind(&Constructor::push_back, bl::var(constructor), bl::var(args))]
    [clear_a(args)];
 
  vector<string> namespaces_return; /// namespace for current return type
  Rule namespace_ret_p = basic.namespace_p[push_back_a(namespaces_return)] >> str_p("::");
 
  ReturnValue retVal0, retVal;
  ReturnValueGrammar returnValue_g(retVal);

  Rule methodName_p = lexeme_d[(upper_p | lower_p)  >> *(alnum_p | '_')];
 
  // template<CALIBRATION = {gtsam::Cal3DS2}>
  Template methodTemplate;
  TemplateGrammar methodTemplate_g(methodTemplate);

  // gtsam::Values retract(const gtsam::VectorValues& delta) const;
  string methodName;
  bool isConst, isConst0 = false;
  Rule method_p =  
    !methodTemplate_g >>
    (returnValue_g >> methodName_p[assign_a(methodName)] >>
     argumentList_g >>
     !str_p("const")[assign_a(isConst,true)] >> ';' >> *basic.comments_p)
    [bl::bind(&Class::addMethod, bl::var(cls), verbose, bl::var(isConst),
        bl::var(methodName), bl::var(args), bl::var(retVal),
        bl::var(methodTemplate))]
    [assign_a(retVal,retVal0)]
    [clear_a(args)]
    [clear_a(methodTemplate)]
    [assign_a(isConst,isConst0)];
 
  Rule staticMethodName_p = lexeme_d[(upper_p | lower_p) >> *(alnum_p | '_')]; 
 
  Rule static_method_p = 
    (str_p("static") >> returnValue_g >> staticMethodName_p[assign_a(methodName)] >>
     argumentList_g >> ';' >> *basic.comments_p)
    [bl::bind(&StaticMethod::addOverload, 
      bl::var(cls.static_methods)[bl::var(methodName)], 
      bl::var(methodName), bl::var(args), bl::var(retVal), boost::none,verbose)]
    [assign_a(retVal,retVal0)]
    [clear_a(args)];
 
  Rule functions_p = constructor_p | method_p | static_method_p; 
 
  // template<CALIBRATION = {gtsam::Cal3DS2}>
  Template classTemplate;
  TemplateGrammar classTemplate_g(classTemplate);

  // Parent class
  Qualified possibleParent;
  TypeGrammar classParent_p(possibleParent);

  // parse a full class
  Rule class_p = 
      eps_p[assign_a(cls,cls0)]
      >> (!(classTemplate_g
          [push_back_a(cls.templateArgs, classTemplate.argName())]
          | templateList_p)
      >> !(str_p("virtual")[assign_a(cls.isVirtual, true)]) 
      >> str_p("class") 
      >> basic.className_p[assign_a(cls.name_)]
      >> ((':' >> classParent_p >> '{')
          [bl::bind(&Class::assignParent, bl::var(cls), bl::var(possibleParent))]
          [clear_a(possibleParent)] | '{')
      >> *(functions_p | basic.comments_p)
      >> str_p("};")) 
      [bl::bind(&Constructor::initializeOrCheck, bl::var(constructor),
          bl::var(cls.name_), boost::none, verbose)]
      [assign_a(cls.constructor, constructor)] 
      [assign_a(cls.namespaces_, namespaces)]
      [assign_a(cls.deconstructor.name,cls.name_)]
      [bl::bind(&handle_possible_template, bl::var(classes), bl::var(cls),
          bl::var(classTemplate.argValues()))]
      [clear_a(classTemplate)]
      [assign_a(constructor, constructor0)] 
      [assign_a(cls,cls0)];
 
  // parse a global function
  Qualified globalFunction;
  Rule global_function_p = 
      (returnValue_g >> staticMethodName_p[assign_a(globalFunction.name_)] >>
       argumentList_g >> ';' >> *basic.comments_p)
      [assign_a(globalFunction.namespaces_,namespaces)]
      [bl::bind(&GlobalFunction::addOverload, 
        bl::var(global_functions)[bl::var(globalFunction.name_)],
        bl::var(globalFunction), bl::var(args), bl::var(retVal), boost::none,verbose)]
      [assign_a(retVal,retVal0)]
      [clear_a(globalFunction)]
      [clear_a(args)];
 
  Rule include_p = str_p("#include") >> ch_p('<') >> (*(anychar_p - '>'))[push_back_a(includes)] >> ch_p('>');

#ifdef __clang__
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wuninitialized"
#endif

  Rule namespace_def_p =
      (str_p("namespace")
      >> basic.namespace_p[push_back_a(namespaces)]
      >> ch_p('{')
      >> *(include_p | class_p | templateSingleInstantiation_p | global_function_p | namespace_def_p | basic.comments_p)
      >> ch_p('}'))
      [pop_a(namespaces)];

#ifdef __clang__
#pragma clang diagnostic pop
#endif

  // parse forward declaration
  ForwardDeclaration fwDec0, fwDec;
  Rule forward_declaration_p =
      !(str_p("virtual")[assign_a(fwDec.isVirtual, true)]) 
      >> str_p("class") 
      >> (*(basic.namespace_p >> str_p("::")) >> basic.className_p)[assign_a(fwDec.name)]
      >> ch_p(';') 
      [push_back_a(forward_declarations, fwDec)] 
      [assign_a(fwDec, fwDec0)]; 
 
  Rule module_content_p = basic.comments_p | include_p | class_p
      | templateSingleInstantiation_p | forward_declaration_p
      | global_function_p | namespace_def_p;
 
  Rule module_p = *module_content_p >> !end_p; 
 
  //----------------------------------------------------------------------------
  // for debugging, define BOOST_SPIRIT_DEBUG
# ifdef BOOST_SPIRIT_DEBUG
  BOOST_SPIRIT_DEBUG_NODE(className_p);
  BOOST_SPIRIT_DEBUG_NODE(classPtr_p);
  BOOST_SPIRIT_DEBUG_NODE(classRef_p);
  BOOST_SPIRIT_DEBUG_NODE(basisType_p);
  BOOST_SPIRIT_DEBUG_NODE(name_p);
  BOOST_SPIRIT_DEBUG_NODE(argument_p);
  BOOST_SPIRIT_DEBUG_NODE(argumentList_g);
  BOOST_SPIRIT_DEBUG_NODE(constructor_p);
  BOOST_SPIRIT_DEBUG_NODE(returnType1_p);
  BOOST_SPIRIT_DEBUG_NODE(returnType2_p);
  BOOST_SPIRIT_DEBUG_NODE(pair_p);
  BOOST_SPIRIT_DEBUG_NODE(void_p);
  BOOST_SPIRIT_DEBUG_NODE(returnValue_g);
  BOOST_SPIRIT_DEBUG_NODE(methodName_p);
  BOOST_SPIRIT_DEBUG_NODE(method_p);
  BOOST_SPIRIT_DEBUG_NODE(class_p);
  BOOST_SPIRIT_DEBUG_NODE(namespace_def_p);
  BOOST_SPIRIT_DEBUG_NODE(module_p);
# endif
  //----------------------------------------------------------------------------
 
  // and parse contents
  parse_info<const char*> info = parse(data.c_str(), module_p, space_p);
  if(!info.full) {
    printf("parsing stopped at \n%.20s\n",info.stop);
    cout << "Stopped near:\n"
      "class '" << cls.name_ << "'\n"
      "method '" << methodName << "'" << endl;
    throw ParseFailed((int)info.length);
  }

  // Post-process classes for serialization markers
  BOOST_FOREACH(Class& cls, classes)
    cls.erase_serialization();

  // Explicitly add methods to the classes from parents so it shows in documentation
  BOOST_FOREACH(Class& cls, classes)
    cls.appendInheritedMethods(cls, classes);

  // Expand templates - This is done first so that template instantiations are
  // counted in the list of valid types, have their attributes and dependencies
  // checked, etc.

  expandedClasses = ExpandTypedefInstantiations(classes,templateInstantiationTypedefs);

  // Dependency check list
  vector<string> validTypes = GenerateValidTypes(expandedClasses,
      forward_declarations);

  // Check that all classes have been defined somewhere
  verifyArguments<GlobalFunction>(validTypes, global_functions);
  verifyReturnTypes<GlobalFunction>(validTypes, global_functions);

  hasSerialiable = false;
  BOOST_FOREACH(const Class& cls, expandedClasses)
    cls.verifyAll(validTypes,hasSerialiable);

  // Create type attributes table and check validity
  typeAttributes.addClasses(expandedClasses);
  typeAttributes.addForwardDeclarations(forward_declarations);
  // add Eigen types as template arguments are also checked ?
  vector<ForwardDeclaration> eigen;
  eigen.push_back(ForwardDeclaration("Vector"));
  eigen.push_back(ForwardDeclaration("Matrix"));
  typeAttributes.addForwardDeclarations(eigen);
  typeAttributes.checkValidity(expandedClasses);
} 
 
/* ************************************************************************* */ 
void Module::matlab_code(const string& toolboxPath) const {

  fs::create_directories(toolboxPath);

  // create the unified .cpp switch file
  const string wrapperName = name + "_wrapper";
  string wrapperFileName = toolboxPath + "/" + wrapperName + ".cpp";
  FileWriter wrapperFile(wrapperFileName, verbose, "//");
  wrapperFile.oss << "#include <wrap/matlab.h>\n";
  wrapperFile.oss << "#include <map>\n";
  wrapperFile.oss << "#include <boost/foreach.hpp>\n";
  wrapperFile.oss << "\n";

  // Include boost.serialization archive headers before other class headers
  if (hasSerialiable) {
    wrapperFile.oss << "#include <boost/serialization/export.hpp>\n";
    wrapperFile.oss << "#include <boost/archive/text_iarchive.hpp>\n";
    wrapperFile.oss << "#include <boost/archive/text_oarchive.hpp>\n\n";
  }

  // Generate includes while avoiding redundant includes
  generateIncludes(wrapperFile);

  // create typedef classes - we put this at the top of the wrap file so that
  // collectors and method arguments can use these typedefs
  BOOST_FOREACH(const Class& cls, expandedClasses)
    if(!cls.typedefName.empty())
      wrapperFile.oss << cls.getTypedef() << "\n";
  wrapperFile.oss << "\n";

  // Generate boost.serialization export flags (needs typedefs from above)
  if (hasSerialiable) {
    wrapperFile.oss << "#define CHART_VALUE_EXPORT(UNIQUE_NAME, TYPE) \\" << "\n";
    wrapperFile.oss << "typedef gtsam::ChartValue<TYPE, gtsam::DefaultChart<TYPE> > UNIQUE_NAME; \\" << "\n";
    wrapperFile.oss << "BOOST_CLASS_EXPORT( UNIQUE_NAME);" << "\n";
    wrapperFile.oss << "\n";

    BOOST_FOREACH(const Class& cls, expandedClasses)
      if(cls.isSerializable) {
        wrapperFile.oss << cls.getSerializationExport() << "\n";
    }

    wrapperFile.oss << "\n";

    // hard coded! just make it work at first commit. Should be removed.
    vector<string> valueTypeClasses;
    valueTypeClasses.push_back("gtsamPoint2");
    valueTypeClasses.push_back("gtsamPoint3");
    valueTypeClasses.push_back("gtsamPose2");
    valueTypeClasses.push_back("gtsamPose3");
    BOOST_FOREACH(const Class& cls, expandedClasses)
      if(cls.isSerializable) {
        if(find(valueTypeClasses.begin(), valueTypeClasses.end(), cls.qualifiedName()) != valueTypeClasses.end() ) {
          wrapperFile.oss << cls.getSerializationChartValueExport() << "\n";
        }
    }
    wrapperFile.oss << "\n";
  }

  // Generate collectors and cleanup function to be called from mexAtExit
  WriteCollectorsAndCleanupFcn(wrapperFile, name, expandedClasses);

  // generate RTTI registry (for returning derived-most types)
  WriteRTTIRegistry(wrapperFile, name, expandedClasses);

  vector<string> functionNames; // Function names stored by index for switch

  // create proxy class and wrapper code
  BOOST_FOREACH(const Class& cls, expandedClasses)
    cls.matlab_proxy(toolboxPath, wrapperName, typeAttributes, wrapperFile, functionNames);

  // create matlab files and wrapper code for global functions
  BOOST_FOREACH(const GlobalFunctions::value_type& p, global_functions)
    p.second.matlab_proxy(toolboxPath, wrapperName, typeAttributes, wrapperFile, functionNames);

  // finish wrapper file
  wrapperFile.oss << "\n";
  finish_wrapper(wrapperFile, functionNames);

  wrapperFile.emit(true);
}

/* ************************************************************************* */ 
void Module::generateIncludes(FileWriter& file) const {

  // collect includes
  vector<string> all_includes(includes);

  // sort and remove duplicates
  sort(all_includes.begin(), all_includes.end());
  vector<string>::const_iterator last_include = unique(all_includes.begin(), all_includes.end());
  vector<string>::const_iterator it = all_includes.begin();
  // add includes to file
  for (; it != last_include; ++it)
    file.oss << "#include <" << *it << ">" << endl;
  file.oss << "\n";
}


/* ************************************************************************* */
  void Module::finish_wrapper(FileWriter& file, const std::vector<std::string>& functionNames) const { 
    file.oss << "void mexFunction(int nargout, mxArray *out[], int nargin, const mxArray *in[])\n"; 
    file.oss << "{\n"; 
    file.oss << "  mstream mout;\n"; // Send stdout to MATLAB console 
    file.oss << "  std::streambuf *outbuf = std::cout.rdbuf(&mout);\n\n"; 
    file.oss << "  _" << name << "_RTTIRegister();\n\n"; 
    file.oss << "  int id = unwrap<int>(in[0]);\n\n"; 
    file.oss << "  try {\n"; 
    file.oss << "    switch(id) {\n"; 
    for(size_t id = 0; id < functionNames.size(); ++id) { 
      file.oss << "    case " << id << ":\n"; 
      file.oss << "      " << functionNames[id] << "(nargout, out, nargin-1, in+1);\n"; 
      file.oss << "      break;\n"; 
    } 
    file.oss << "    }\n"; 
    file.oss << "  } catch(const std::exception& e) {\n"; 
    file.oss << "    mexErrMsgTxt((\"Exception from gtsam:\\n\" + std::string(e.what()) + \"\\n\").c_str());\n"; 
    file.oss << "  }\n"; 
    file.oss << "\n"; 
    file.oss << "  std::cout.rdbuf(outbuf);\n"; // Restore cout 
    file.oss << "}\n"; 
  } 
 
/* ************************************************************************* */ 
vector<Class> Module::ExpandTypedefInstantiations(const vector<Class>& classes, const vector<TemplateInstantiationTypedef> instantiations) {
 
  vector<Class> expandedClasses = classes;
 
  BOOST_FOREACH(const TemplateInstantiationTypedef& inst, instantiations) { 
    // Add the new class to the list 
    expandedClasses.push_back(inst.findAndExpand(classes));
  }
 
  // Remove all template classes for expandedClasses
  for(size_t i = 0; i < expandedClasses.size(); ++i) 
    if(!expandedClasses[size_t(i)].templateArgs.empty()) { 
      expandedClasses.erase(expandedClasses.begin() + size_t(i)); 
      -- i; 
    } 
 
  return expandedClasses; 
}

/* ************************************************************************* */ 
vector<string> Module::GenerateValidTypes(const vector<Class>& classes, const vector<ForwardDeclaration> forwardDeclarations) { 
  vector<string> validTypes; 
  BOOST_FOREACH(const ForwardDeclaration& fwDec, forwardDeclarations) { 
    validTypes.push_back(fwDec.name);
  } 
  validTypes.push_back("void"); 
  validTypes.push_back("string"); 
  validTypes.push_back("int"); 
  validTypes.push_back("bool"); 
  validTypes.push_back("char"); 
  validTypes.push_back("unsigned char"); 
  validTypes.push_back("size_t"); 
  validTypes.push_back("double"); 
  validTypes.push_back("Vector"); 
  validTypes.push_back("Matrix"); 
  //Create a list of parsed classes for dependency checking 
  BOOST_FOREACH(const Class& cls, classes) { 
    validTypes.push_back(cls.qualifiedName("::")); 
  } 
 
  return validTypes; 
} 
 
/* ************************************************************************* */ 
void Module::WriteCollectorsAndCleanupFcn(FileWriter& wrapperFile, const std::string& moduleName, const std::vector<Class>& classes) { 
  // Generate all collectors 
  BOOST_FOREACH(const Class& cls, classes) { 
    const string matlabUniqueName = cls.qualifiedName(), 
      cppName = cls.qualifiedName("::"); 
    wrapperFile.oss << "typedef std::set<boost::shared_ptr<" << cppName << ">*> " 
      << "Collector_" << matlabUniqueName << ";\n"; 
    wrapperFile.oss << "static Collector_" << matlabUniqueName << 
      " collector_" << matlabUniqueName << ";\n"; 
  } 
 
  // generate mexAtExit cleanup function 
  wrapperFile.oss << 
    "\nvoid _deleteAllObjects()\n" 
    "{\n" 
    "  mstream mout;\n" // Send stdout to MATLAB console 
    "  std::streambuf *outbuf = std::cout.rdbuf(&mout);\n\n" 
    "  bool anyDeleted = false;\n"; 
  BOOST_FOREACH(const Class& cls, classes) { 
    const string matlabUniqueName = cls.qualifiedName(); 
    const string cppName = cls.qualifiedName("::"); 
    const string collectorType = "Collector_" + matlabUniqueName; 
    const string collectorName = "collector_" + matlabUniqueName; 
    // The extra curly-braces around the for loops work around a limitation in MSVC (existing 
    // since 2005!) preventing more than 248 blocks. 
    wrapperFile.oss << 
      "  { for(" << collectorType << "::iterator iter = " << collectorName << ".begin();\n" 
      "      iter != " << collectorName << ".end(); ) {\n" 
      "    delete *iter;\n" 
      "    " << collectorName << ".erase(iter++);\n" 
      "    anyDeleted = true;\n" 
      "  } }\n"; 
  } 
  wrapperFile.oss << 
    "  if(anyDeleted)\n" 
    "    cout <<\n" 
    "      \"WARNING:  Wrap modules with variables in the workspace have been reloaded due to\\n\"\n" 
    "      \"calling destructors, call 'clear all' again if you plan to now recompile a wrap\\n\"\n" 
    "      \"module, so that your recompiled module is used instead of the old one.\" << endl;\n" 
    "  std::cout.rdbuf(outbuf);\n" // Restore cout 
    "}\n\n"; 
} 
 
/* ************************************************************************* */ 
void Module::WriteRTTIRegistry(FileWriter& wrapperFile, const std::string& moduleName, const std::vector<Class>& classes) { 
  wrapperFile.oss << 
    "void _" << moduleName << "_RTTIRegister() {\n" 
    "  const mxArray *alreadyCreated = mexGetVariablePtr(\"global\", \"gtsam_" + moduleName + "_rttiRegistry_created\");\n" 
    "  if(!alreadyCreated) {\n" 
    "    std::map<std::string, std::string> types;\n"; 
  BOOST_FOREACH(const Class& cls, classes) { 
    if(cls.isVirtual) 
      wrapperFile.oss << 
      "    types.insert(std::make_pair(typeid(" << cls.qualifiedName("::") << ").name(), \"" << cls.qualifiedName(".") << "\"));\n"; 
  } 
  wrapperFile.oss << "\n"; 
 
  wrapperFile.oss << 
    "    mxArray *registry = mexGetVariable(\"global\", \"gtsamwrap_rttiRegistry\");\n" 
    "    if(!registry)\n" 
    "      registry = mxCreateStructMatrix(1, 1, 0, NULL);\n" 
    "    typedef std::pair<std::string, std::string> StringPair;\n" 
    "    BOOST_FOREACH(const StringPair& rtti_matlab, types) {\n" 
    "      int fieldId = mxAddField(registry, rtti_matlab.first.c_str());\n" 
    "      if(fieldId < 0)\n" 
    "        mexErrMsgTxt(\"gtsam wrap:  Error indexing RTTI types, inheritance will not work correctly\");\n" 
    "      mxArray *matlabName = mxCreateString(rtti_matlab.second.c_str());\n" 
    "      mxSetFieldByNumber(registry, 0, fieldId, matlabName);\n" 
    "    }\n" 
    "    if(mexPutVariable(\"global\", \"gtsamwrap_rttiRegistry\", registry) != 0)\n" 
    "      mexErrMsgTxt(\"gtsam wrap:  Error indexing RTTI types, inheritance will not work correctly\");\n" 
    "    mxDestroyArray(registry);\n" 
    "    \n" 
    "    mxArray *newAlreadyCreated = mxCreateNumericMatrix(0, 0, mxINT8_CLASS, mxREAL);\n" 
    "    if(mexPutVariable(\"global\", \"gtsam_" + moduleName + "_rttiRegistry_created\", newAlreadyCreated) != 0)\n" 
    "      mexErrMsgTxt(\"gtsam wrap:  Error indexing RTTI types, inheritance will not work correctly\");\n" 
    "    mxDestroyArray(newAlreadyCreated);\n" 
    "  }\n" 
    "}\n" 
    "\n"; 
} 
 
/* ************************************************************************* */ 
void Module::python_wrapper(const string& toolboxPath) const {

  fs::create_directories(toolboxPath);

  // create the unified .cpp switch file
  const string wrapperName = name + "_python";
  string wrapperFileName = toolboxPath + "/" + wrapperName + ".cpp";
  FileWriter wrapperFile(wrapperFileName, verbose, "//");
  wrapperFile.oss << "#include <boost/python.hpp>\n\n";
  wrapperFile.oss << "using namespace boost::python;\n";
  wrapperFile.oss << "BOOST_PYTHON_MODULE(" + name + ")\n";
  wrapperFile.oss << "{\n";

  // write out classes
  BOOST_FOREACH(const Class& cls, expandedClasses)
    cls.python_wrapper(wrapperFile);

  // write out global functions
  BOOST_FOREACH(const GlobalFunctions::value_type& p, global_functions)
    p.second.python_wrapper(wrapperFile);

  // finish wrapper file
  wrapperFile.oss << "}\n";

  wrapperFile.emit(true);
}

/* ************************************************************************* */
