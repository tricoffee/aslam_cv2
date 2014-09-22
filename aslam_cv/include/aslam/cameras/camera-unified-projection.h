#ifndef ASLAM_UNIFIED_PROJECTION_CAMERA_H_
#define ASLAM_UNIFIED_PROJECTION_CAMERA_H_

#include <aslam/cameras/camera.h>
#include <aslam/common/crtp-clone.h>
#include <aslam/cameras/distortion.h>
#include <aslam/common/macros.h>

namespace aslam {

/// \class UnifiedProjectionCamera
/// \brief An implementation of the unified projection camera model with (optional) distortion.
///
///  Intrinsic parameters ordering: xi, fu, fv, cu, cv
///
///  Reference: (1) C. Geyer and K. Daniilidis. A unifying theory for central panoramic systems
///                 and practical implications. In ECCV, pages 445–461, 2000.
///                 (http://www.frc.ri.cmu.edu/users/cgeyer/papers/geyer_eccv00.pdf)
///             (2) Joao P. Barreto and Helder Araujo. Issues on the geometry of central
///                 catadioptric image formation. In CVPR, volume 2, pages 422–427, 2001.
///                 (http://home.isr.uc.pt/~jpbar/Publication_Source/cvpr2001.pdf)
class UnifiedProjectionCamera : public aslam::Cloneable<Camera, UnifiedProjectionCamera> {
  enum { kNumOfParams = 5 };
 public:
  ASLAM_POINTER_TYPEDEFS(UnifiedProjectionCamera);

  enum { CLASS_SERIALIZATION_VERSION = 1 };
  EIGEN_MAKE_ALIGNED_OPERATOR_NEW

  // TODO(slynen) Enable commented out PropertyTree support
  // UnifiedProjectionCamera(const sm::PropertyTree& config);

  //////////////////////////////////////////////////////////////
  /// \name Constructors/destructors and operators
  /// @{

 protected:
  /// \brief Empty constructor for serialization interface.
  UnifiedProjectionCamera();

 public:
  /// Copy constructor for clone operation.
  UnifiedProjectionCamera(const UnifiedProjectionCamera& other) : Base(other), distortion_(nullptr) {
    if (other.distortion_) // Clone distortion if model is set.
      distortion_.reset(other.distortion_->clone());
  };

  void operator=(const UnifiedProjectionCamera&) = delete;

 public:
  /// \brief Construct a camera with distortion.
  /// @param[in] intrinsics   vector containing the intrinsic parameters (xi,fu,fv,cu.cv)
  /// @param[in] image_height image height in pixels
  /// @param[in] distortion   pointer to the distortion model
  UnifiedProjectionCamera(const Eigen::VectorXd& intrinsics, uint32_t image_width,
                          uint32_t image_height, aslam::Distortion::Ptr distortion);

  /// \brief Construct a camera without distortion.
  /// @param[in] intrinsics   vector containing the intrinsic parameters (xi,fu,fv,cu.cv)
  /// @param[in] image_width  image width in pixels
  /// @param[in] image_height image height in pixels
  /// @param[in] distortion   pointer to the distortion model
  UnifiedProjectionCamera(const Eigen::VectorXd& intrinsics, uint32_t image_width,
                          uint32_t image_height);

  /// \brief Construct a camera with distortion.
  /// @param[in] xi               mirror parameter
  /// @param[in] focallength_cols focallength in pixels; cols (width-direction)
  /// @param[in] focallength_rows focallength in pixels; rows (height-direction)
  /// @param[in] imagecenter_cols image center in pixels; cols (width-direction)
  /// @param[in] imagecenter_rows image center in pixels; rows (height-direction)
  /// @param[in] image_width      image width in pixels
  /// @param[in] image_height     image height in pixels
  /// @param[in] distortion       pointer to the distortion model
  UnifiedProjectionCamera(double xi, double focallength_cols, double focallength_rows,
                          double imagecenter_cols, double imagecenter_rows,
                          uint32_t image_width, uint32_t image_height,
                          aslam::Distortion::Ptr distortion);

  /// \brief Construct a camera without distortion.
  /// @param[in] xi               mirror parameter
  /// @param[in] focallength_cols focallength in pixels; cols (width-direction)
  /// @param[in] focallength_rows focallength in pixels; rows (height-direction)
  /// @param[in] imagecenter_cols image center in pixels; cols (width-direction)
  /// @param[in] imagecenter_rows image center in pixels; rows (height-direction)
  /// @param[in] image_width      image width in pixels
  /// @param[in] image_height     image height in pixels
  UnifiedProjectionCamera(double xi, double focallength_cols, double focallength_rows,
                          double imagecenter_cols, double imagecenter_rows,
                          uint32_t image_width, uint32_t image_height);

  virtual ~UnifiedProjectionCamera() {};

  /// \brief Compare this camera to another camera object.
  virtual bool operator==(const Camera& other) const;

  /// \brief Convenience function to print the state using streams.
  std::ostream& operator<<(std::ostream& out) {
    this->printParameters(out, std::string(""));
    return out;
  };

  /// @}

  //////////////////////////////////////////////////////////////
  /// \name Methods to project and back-project euclidean points
  /// @{

  /// \brief Compute the 3d bearing vector in euclidean coordinates given a keypoint in
  ///        image coordinates. Uses the projection (& distortion) models.
  /// @param[in]  keypoint     Keypoint in image coordinates.
  /// @param[out] out_point_3d Bearing vector in euclidean coordinates (with z=1 -> non-normalized).
  virtual bool backProject3(const Eigen::Vector2d& keypoint, Eigen::Vector3d* out_point_3d) const;

  /// \brief Checks the success of a projection operation and returns the result in a
  ///        ProjectionResult object.
  /// @param[in] keypoint Keypoint in image coordinates.
  /// @param[in] point_3d Projected point in euclidean.
  /// @return The ProjectionResult object contains details about the success of the projection.
  const ProjectionResult evaluateProjectionResult(const Eigen::Vector2d& keypoint,
                                                  const Eigen::Vector3d& point_3d) const;


  /// \brief Checks whether an undistorted keypoint lies in the valid range.
  /// @param[in] keypoint Squarred norm of the normalized undistorted keypoint.
  /// @param[in] xi       Mirror parameter
  bool isUndistortedKeypointValid(const double& rho2_d, const double& xi) const;

  /// \brief Checks whether a keypoint is liftable to the unit sphere.
  /// @param[in] keypoint Keypoint in image coordinates.
  bool isLiftable(const Eigen::Vector2d& keypoint) const;

  /// @}

  //////////////////////////////////////////////////////////////
  /// \name Functional methods to project and back-project points
  /// @{

  // Get the overloaded non-virtual project3Functional(..) from base into scope.
  using Camera::project3Functional;

  /// \brief This function projects a point into the image using the intrinsic parameters
  ///        that are passed in as arguments. If any of the Jacobians are nonnull, they
  ///        should be filled in with the Jacobian with respect to small changes in the argument.
  /// @param[in]  point_3d                The point in euclidean coordinates.
  /// @param[in]  intrinsics_external     External intrinsic parameter vector.
  ///                                     NOTE: If nullptr, use internal distortion parameters.
  /// @param[in]  distortion_coefficients_external External distortion parameter vector.
  ///                                     Parameter is ignored is no distortion is active.
  ///                                     NOTE: If nullptr, use internal distortion parameters.
  /// @param[out] out_keypoint            The keypoint in image coordinates.
  /// @param[out] out_jacobian_point      The Jacobian wrt. to changes in the euclidean point.
  ///                                       nullptr: calculation is skipped.
  /// @param[out] out_jacobian_intrinsics The Jacobian wrt. to changes in the intrinsics.
  ///                                       nullptr: calculation is skipped.
  /// @param[out] out_jacobian_distortion The Jacobian wrt. to changes in the distortion parameters.
  ///                                       nullptr: calculation is skipped.
  /// @return Contains information about the success of the projection. Check
  ///         \ref ProjectionResult for more information.
  virtual const ProjectionResult project3Functional(
      const Eigen::Vector3d& point_3d,
      const Eigen::VectorXd* intrinsics_external,
      const Eigen::VectorXd* distortion_coefficients_external,
      Eigen::Vector2d* out_keypoint,
      Eigen::Matrix<double, 2, 3>* out_jacobian_point,
      Eigen::Matrix<double, 2, Eigen::Dynamic>* out_jacobian_intrinsics,
      Eigen::Matrix<double, 2, Eigen::Dynamic>* out_jacobian_distortion) const;

  /// @}

  //////////////////////////////////////////////////////////////
  /// \name Methods to support unit testing.
  /// @{

  /// \brief Creates a random valid keypoint..
  virtual Eigen::Vector2d createRandomKeypoint() const;

  /// \brief Creates a random visible point. Negative depth means random between
  ///        0 and 100 meters.
  virtual Eigen::Vector3d createRandomVisiblePoint(double depth) const;

  /// \brief Create a test camera object for unit testing.
  template<typename DistortionType>
  static UnifiedProjectionCamera::Ptr createTestCamera()   {
    return UnifiedProjectionCamera::Ptr(new UnifiedProjectionCamera(0.9, 400, 400, 320, 240, 640, 480,
                                          DistortionType::createTestDistortion()));
  }

  /// \brief Create a test camera object for unit testing. (without distortion)
  static UnifiedProjectionCamera::Ptr createTestCamera() {
    return UnifiedProjectionCamera::Ptr(new UnifiedProjectionCamera(0.9, 400, 400, 320, 240, 640, 480));
  }

  /// @}

  //////////////////////////////////////////////////////////////
  /// \name Methods to set/get distortion parameters.
  /// @{

  /// \brief Returns a pointer to the underlying distortion object.
  /// @return ptr to distortion model; nullptr if none is set or not available
  ///         for the camera type
  virtual aslam::Distortion::Ptr distortion() { return distortion_; };

  /// \brief Returns a const pointer to the underlying distortion object.
  /// @return const_ptr to distortion model; nullptr if none is set or not available
  ///         for the camera type
  virtual const aslam::Distortion::Ptr distortion() const { return distortion_; };

  /// @}

  //////////////////////////////////////////////////////////////
  /// \name Methods to access intrinsics.
  /// @{

  /// \brief The horizontal focal length in pixels.
  double xi() const { return intrinsics_[0]; };
  /// \brief The horizontal focal length in pixels.
  double fu() const { return intrinsics_[1]; };
  /// \brief The vertical focal length in pixels.
  double fv() const { return intrinsics_[2]; };
  /// \brief The horizontal image center in pixels.
  double cu() const { return intrinsics_[3]; };
  /// \brief The vertical image center in pixels.
  double cv() const { return intrinsics_[4]; };
  /// \brief Returns the fov parameter.
  double fov_parameter(double xi) const { return (xi <= 1.0) ? xi : (1 / xi); };

  /// \brief Returns the number of intrinsic parameters used in this camera model.
  inline static constexpr int parameterCount() {
      return kNumOfParams;
  }

  /// \brief The number of intrinsic parameters.
  virtual int getParameterSize() const {
    return kNumOfParams;
  }

  /// \brief Print the internal parameters of the camera in a human-readable form
  /// Print to the ostream that is passed in. The text is extra
  /// text used by the calling function to distinguish cameras
  virtual void printParameters(std::ostream& out, const std::string& text) const;

  /// @}

 private:
  /// \brief The distortion of this camera.
  aslam::Distortion::Ptr distortion_;

  /// \brief Minimal depth for a valid projection.
  static constexpr double kMinimumDepth = 1e-10;
};

}  // namespace aslam

#endif  // ASLAM_UNIFIED_PROJECTION_CAMERA_H_
