/* ----------------------------------------------------------------------------

 * GTSAM Copyright 2010, Georgia Tech Research Corporation,
 * Atlanta, Georgia 30332-0415
 * All Rights Reserved
 * Authors: Frank Dellaert, et al. (see THANKS for the full author list)

 * See LICENSE for the license information

 * -------------------------------------------------------------------------- */

/**
 * @file    ISAM2-impl.cpp
 * @brief   Incremental update functionality (ISAM2) for BayesTree, with fluid relinearization.
 * @author  Michael Kaess
 * @author  Richard Roberts
 */

#include <gtsam/nonlinear/ISAM2-impl.h>
#include <gtsam/inference/Symbol.h> // for selective linearization thresholds
#include <gtsam/base/debug.h>
#include <functional>
#include <boost/range/adaptors.hpp>

using namespace std;

namespace gtsam {

/* ************************************************************************* */
void ISAM2::Impl::AddVariables(
    const Values& newTheta, Values& theta, VectorValues& delta,
    VectorValues& deltaNewton, VectorValues& RgProd,
    const KeyFormatter& keyFormatter)
{
  const bool debug = ISDEBUG("ISAM2 AddVariables");

  theta.insert(newTheta);
  if(debug) newTheta.print("The new variables are: ");
  // Add zeros into the VectorValues
  delta.insert(newTheta.zeroVectors());
  deltaNewton.insert(newTheta.zeroVectors());
  RgProd.insert(newTheta.zeroVectors());
}

/* ************************************************************************* */
void ISAM2::Impl::RemoveVariables(const FastSet<Key>& unusedKeys, const FastVector<ISAM2::sharedClique>& roots,
                                  Values& theta, VariableIndex& variableIndex,
                                  VectorValues& delta, VectorValues& deltaNewton, VectorValues& RgProd,
                                  FastSet<Key>& replacedKeys, Base::Nodes& nodes,
                                  FastSet<Key>& fixedVariables)
{
  variableIndex.removeUnusedVariables(unusedKeys.begin(), unusedKeys.end());
  BOOST_FOREACH(Key key, unusedKeys) {
    delta.erase(key);
    deltaNewton.erase(key);
    RgProd.erase(key);
    replacedKeys.erase(key);
    nodes.unsafe_erase(key);
    theta.erase(key);
    fixedVariables.erase(key);
  }
}

/* ************************************************************************* */
FastSet<Key> ISAM2::Impl::CheckRelinearizationFull(const VectorValues& delta,
    const ISAM2Params::RelinearizationThreshold& relinearizeThreshold)
{
  FastSet<Key> relinKeys;

  if(const double* threshold = boost::get<double>(&relinearizeThreshold))
  {
    BOOST_FOREACH(const VectorValues::KeyValuePair& key_delta, delta) {
      double maxDelta = key_delta.second.lpNorm<Eigen::Infinity>();
      if(maxDelta >= *threshold)
        relinKeys.insert(key_delta.first);
    }
  }
  else if(const FastMap<char,Vector>* thresholds = boost::get<FastMap<char,Vector> >(&relinearizeThreshold))
  {
    BOOST_FOREACH(const VectorValues::KeyValuePair& key_delta, delta) {
      const Vector& threshold = thresholds->find(Symbol(key_delta.first).chr())->second;
      if(threshold.rows() != key_delta.second.rows())
        throw std::invalid_argument("Relinearization threshold vector dimensionality for '" + std::string(1, Symbol(key_delta.first).chr()) + "' passed into iSAM2 parameters does not match actual variable dimensionality.");
      if((key_delta.second.array().abs() > threshold.array()).any())
        relinKeys.insert(key_delta.first);
    }
  }

  return relinKeys;
}

/* ************************************************************************* */
void CheckRelinearizationRecursiveDouble(FastSet<Key>& relinKeys, double threshold,
                                         const VectorValues& delta, const ISAM2Clique::shared_ptr& clique)
{
  // Check the current clique for relinearization
  bool relinearize = false;
  BOOST_FOREACH(Key var, *clique->conditional()) {
    double maxDelta = delta[var].lpNorm<Eigen::Infinity>();
    if(maxDelta >= threshold) {
      relinKeys.insert(var);
      relinearize = true;
    }
  }

  // If this node was relinearized, also check its children
  if(relinearize) {
    BOOST_FOREACH(const ISAM2Clique::shared_ptr& child, clique->children) {
      CheckRelinearizationRecursiveDouble(relinKeys, threshold, delta, child);
    }
  }
}

/* ************************************************************************* */
void CheckRelinearizationRecursiveMap(FastSet<Key>& relinKeys, const FastMap<char,Vector>& thresholds,
                                      const VectorValues& delta,
                                      const ISAM2Clique::shared_ptr& clique)
{
  // Check the current clique for relinearization
  bool relinearize = false;
  BOOST_FOREACH(Key var, *clique->conditional()) {
    // Find the threshold for this variable type
    const Vector& threshold = thresholds.find(Symbol(var).chr())->second;

    const Vector& deltaVar = delta[var];

    // Verify the threshold vector matches the actual variable size
    if(threshold.rows() != deltaVar.rows())
      throw std::invalid_argument("Relinearization threshold vector dimensionality for '" + std::string(1, Symbol(var).chr()) + "' passed into iSAM2 parameters does not match actual variable dimensionality.");

    // Check for relinearization
    if((deltaVar.array().abs() > threshold.array()).any()) {
      relinKeys.insert(var);
      relinearize = true;
    }
  }

  // If this node was relinearized, also check its children
  if(relinearize) {
    BOOST_FOREACH(const ISAM2Clique::shared_ptr& child, clique->children) {
      CheckRelinearizationRecursiveMap(relinKeys, thresholds, delta, child);
    }
  }
}

/* ************************************************************************* */
FastSet<Key> ISAM2::Impl::CheckRelinearizationPartial(const FastVector<ISAM2::sharedClique>& roots,
                                                        const VectorValues& delta,
                                                        const ISAM2Params::RelinearizationThreshold& relinearizeThreshold)
{
  FastSet<Key> relinKeys;
  BOOST_FOREACH(const ISAM2::sharedClique& root, roots) {
    if(relinearizeThreshold.type() == typeid(double))
      CheckRelinearizationRecursiveDouble(relinKeys, boost::get<double>(relinearizeThreshold), delta, root);
    else if(relinearizeThreshold.type() == typeid(FastMap<char,Vector>))
      CheckRelinearizationRecursiveMap(relinKeys, boost::get<FastMap<char,Vector> >(relinearizeThreshold), delta, root);
  }
  return relinKeys;
}

/* ************************************************************************* */
void ISAM2::Impl::FindAll(ISAM2Clique::shared_ptr clique, FastSet<Key>& keys, const FastSet<Key>& markedMask)
{
  static const bool debug = false;
  // does the separator contain any of the variables?
  bool found = false;
  BOOST_FOREACH(Key key, clique->conditional()->parents()) {
    if (markedMask.exists(key)) {
      found = true;
      break;
    }
  }
  if (found) {
    // then add this clique
    keys.insert(clique->conditional()->beginFrontals(), clique->conditional()->endFrontals());
    if(debug) clique->print("Key(s) marked in clique ");
    if(debug) cout << "so marking key " << clique->conditional()->front() << endl;
  }
  BOOST_FOREACH(const ISAM2Clique::shared_ptr& child, clique->children) {
    FindAll(child, keys, markedMask);
  }
}

/* ************************************************************************* */
void ISAM2::Impl::ExpmapMasked(Values& values, const VectorValues& delta,
    const FastSet<Key>& mask, boost::optional<VectorValues&> invalidateIfDebug, const KeyFormatter& keyFormatter)
{
  // If debugging, invalidate if requested, otherwise do not invalidate.
  // Invalidating means setting expmapped entries to Inf, to trigger assertions
  // if we try to re-use them.
#ifdef NDEBUG
  invalidateIfDebug = boost::none;
#endif

  assert(values.size() == delta.size());
  Values::iterator key_value;
  VectorValues::const_iterator key_delta;
#ifdef GTSAM_USE_TBB
  for(key_value = values.begin(); key_value != values.end(); ++key_value)
  {
    key_delta = delta.find(key_value->key);
#else
  for(key_value = values.begin(), key_delta = delta.begin(); key_value != values.end(); ++key_value, ++key_delta)
  {
    assert(key_value->key == key_delta->first);
#endif
    Key var = key_value->key;
    assert(delta[var].size() == (int)key_value->value.dim());
    assert(delta[var].allFinite());
    if(mask.exists(var)) {
      Value* retracted = key_value->value.retract_(delta[var]);
      key_value->value = *retracted;
      retracted->deallocate_();
      if(invalidateIfDebug)
        (*invalidateIfDebug)[var].operator=(Vector::Constant(delta[var].rows(), numeric_limits<double>::infinity())); // Strange syntax to work with clang++ (bug in clang?)
    }
  }
}

/* ************************************************************************* */
namespace internal {
inline static void optimizeInPlace(const boost::shared_ptr<ISAM2Clique>& clique, VectorValues& result) {
  // parents are assumed to already be solved and available in result
  result.update(clique->conditional()->solve(result));

  // starting from the root, call optimize on each conditional
  BOOST_FOREACH(const boost::shared_ptr<ISAM2Clique>& child, clique->children)
    optimizeInPlace(child, result);
}
}

/* ************************************************************************* */
size_t ISAM2::Impl::UpdateDelta(const FastVector<ISAM2::sharedClique>& roots, FastSet<Key>& replacedKeys, VectorValues& delta, double wildfireThreshold) {

  size_t lastBacksubVariableCount;

  if (wildfireThreshold <= 0.0) {
    // Threshold is zero or less, so do a full recalculation
    BOOST_FOREACH(const ISAM2::sharedClique& root, roots)
      internal::optimizeInPlace(root, delta);
    lastBacksubVariableCount = delta.size();

  } else {
    // Optimize with wildfire
    lastBacksubVariableCount = 0;
    BOOST_FOREACH(const ISAM2::sharedClique& root, roots)
      lastBacksubVariableCount += optimizeWildfireNonRecursive(
      root, wildfireThreshold, replacedKeys, delta); // modifies delta

#ifdef GTSAM_EXTRA_CONSISTENCY_CHECKS
    for(size_t j=0; j<delta.size(); ++j)
      assert(delta[j].unaryExpr(ptr_fun(isfinite<double>)).all());
#endif
  }

  // Clear replacedKeys
  replacedKeys.clear();

  return lastBacksubVariableCount;
}

/* ************************************************************************* */
namespace internal {
void updateDoglegDeltas(const boost::shared_ptr<ISAM2Clique>& clique, const FastSet<Key>& replacedKeys,
    const VectorValues& grad, VectorValues& RgProd, size_t& varsUpdated) {

  // Check if any frontal or separator keys were recalculated, if so, we need
  // update deltas and recurse to children, but if not, we do not need to
  // recurse further because of the running separator property.
  bool anyReplaced = false;
  BOOST_FOREACH(Key j, *clique->conditional()) {
    if(replacedKeys.exists(j)) {
      anyReplaced = true;
      break;
    }
  }

  if(anyReplaced) {
    // Update the current variable
    // Get VectorValues slice corresponding to current variables
    Vector gR = grad.vector(FastVector<Key>(clique->conditional()->beginFrontals(), clique->conditional()->endFrontals()));
    Vector gS = grad.vector(FastVector<Key>(clique->conditional()->beginParents(), clique->conditional()->endParents()));

    // Compute R*g and S*g for this clique
    Vector RSgProd = clique->conditional()->get_R() * gR + clique->conditional()->get_S() * gS;

    // Write into RgProd vector
    DenseIndex vectorPosition = 0;
    BOOST_FOREACH(Key frontal, clique->conditional()->frontals()) {
      Vector& RgProdValue = RgProd[frontal];
      RgProdValue = RSgProd.segment(vectorPosition, RgProdValue.size());
      vectorPosition += RgProdValue.size();
    }

    // Now solve the part of the Newton's method point for this clique (back-substitution)
    //(*clique)->solveInPlace(deltaNewton);

    varsUpdated += clique->conditional()->nrFrontals();

    // Recurse to children
    BOOST_FOREACH(const ISAM2Clique::shared_ptr& child, clique->children) {
      updateDoglegDeltas(child, replacedKeys, grad, RgProd, varsUpdated); }
  }
}
}

/* ************************************************************************* */
size_t ISAM2::Impl::UpdateDoglegDeltas(const ISAM2& isam, double wildfireThreshold, FastSet<Key>& replacedKeys,
    VectorValues& deltaNewton, VectorValues& RgProd) {

  // Get gradient
  VectorValues grad;
  gradientAtZero(isam, grad);

  // Update variables
  size_t varsUpdated = 0;
  BOOST_FOREACH(const ISAM2::sharedClique& root, isam.roots())
  {
    internal::updateDoglegDeltas(root, replacedKeys, grad, RgProd, varsUpdated);
    optimizeWildfireNonRecursive(root, wildfireThreshold, replacedKeys, deltaNewton);
  }

  replacedKeys.clear();

  return varsUpdated;
}

}
