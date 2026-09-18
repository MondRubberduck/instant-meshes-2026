/*
    solver_interface.h: Abstract interface for field optimization solvers.

    This file is part of the modernization of Instant Meshes.
*/

#pragma once

#include "common.h"

class Serializer;

class SolverInterface {
public:
  virtual ~SolverInterface() = default;

  /* Core optimization methods */
  virtual void optimizeOrientations(int level) = 0;
  virtual void optimizePositions(int level) = 0;

  /* Control methods */
  virtual void stop() = 0;
  virtual void wait() = 0;
  virtual void shutdown() = 0;
  virtual bool active() = 0;

  /* State management */
  virtual void save(Serializer &state) = 0;
  virtual void load(const Serializer &state) = 0;
  virtual void notify() = 0;

  /* Configuration */
  virtual void setExtrinsic(bool extrinsic) = 0;
  virtual bool extrinsic() const = 0;

  virtual void setRoSy(int rosy) = 0;
  virtual int rosy() const = 0;

  virtual void setPoSy(int posy) = 0;
  virtual int posy() const = 0;

  virtual void setLevel(int level) = 0;
  virtual int level() const = 0;

  /* Progress and Debug */
  virtual Float progress() const = 0;

  /* Interaction */
  virtual void moveSingularity(const std::vector<uint32_t> &path,
                               bool orientations) = 0;
};
