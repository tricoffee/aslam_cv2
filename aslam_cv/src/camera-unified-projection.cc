#include <memory>

#include <aslam/cameras/camera-unified-projection.h>

#include <aslam/cameras/camera-factory.h>
#include <aslam/cameras/camera-pinhole.h>
#include <aslam/common/types.h>
#include <aslam/common/undistort-helpers.h>
#include <aslam/pipeline/undistorter-mapped.h>

// TODO(slynen) Enable commented out PropertyTree support
//#include <sm/PropertyTree.hpp>

namespace aslam {
// TODO(slynen) Enable commented out PropertyTree support
//UnifiedProjection::UnifiedProjection(
//    const sm::PropertyTree & config)
//: Camera(config) {
//  _fu = config.getDouble("fu");
//  _fv = config.getDouble("fv");
//  _cu = config.getDouble("cu");
//  _cv = config.getDouble("cv");
//  imageWidth() = config.getInt("ru");
//  imageHeight() = config.getInt("rv");
//
//  //TODO(slynen): Load and instantiate correct distortion here.
//  // distortion.(config, "distortion")
//  CHECK(false) << "Loading of distortion from property tree not implemented.";
//
//}

UnifiedProjectionCamera::UnifiedProjectionCamera()
    : Base((Eigen::Matrix<double, 5, 1>() << 0.0, 0.0, 0.0, 0.0, 0.0).finished(), 0, 0,
           Camera::Type::kUnifiedProjection) {
}

UnifiedProjectionCamera::UnifiedProjectionCamera(const Eigen::VectorXd& intrinsics,
                                                 uint32_t image_width, uint32_t image_height,
                                                 aslam::Distortion::UniquePtr& distortion)
    : Base(intrinsics, distortion, image_width, image_height,
           Camera::Type::kUnifiedProjection) {
  CHECK(intrinsicsValid(intrinsics));
}

UnifiedProjectionCamera::UnifiedProjectionCamera(const Eigen::VectorXd& intrinsics,
                                                 uint32_t image_width, uint32_t image_height)
    : Base(intrinsics, image_width, image_height, Camera::Type::kUnifiedProjection) {
  CHECK(intrinsicsValid(intrinsics));
}

UnifiedProjectionCamera::UnifiedProjectionCamera(double xi, double focallength_cols,
                                                 double focallength_rows, double imagecenter_cols,
                                                 double imagecenter_rows, uint32_t image_width,
                                                 uint32_t image_height,
                                                 aslam::Distortion::UniquePtr& distortion)
    : UnifiedProjectionCamera(
        (Eigen::Matrix<double, 5, 1>() << xi, focallength_cols, focallength_rows, imagecenter_cols,
            imagecenter_rows).finished(), image_width, image_height, distortion) {}

UnifiedProjectionCamera::UnifiedProjectionCamera(double xi, double focallength_cols,
                                                 double focallength_rows, double imagecenter_cols,
                                                 double imagecenter_rows, uint32_t image_width,
                                                 uint32_t image_height)
    : UnifiedProjectionCamera(
        (Eigen::Matrix<double, 5, 1>() << xi, focallength_cols, focallength_rows, imagecenter_cols,
            imagecenter_rows).finished(), image_width, image_height) {}

bool UnifiedProjectionCamera::operator==(const Camera& other) const {
  // Check that the camera models are the same.
  const UnifiedProjectionCamera* rhs = dynamic_cast<const UnifiedProjectionCamera*>(&other);
  if (!rhs)
    return false;

  // Verify that the base members are equal.
  if (!Camera::operator==(other))
    return false;

  // Check if only one camera defines a distortion.
  if ((distortion_ && !rhs->distortion_) || (!distortion_ && rhs->distortion_))
    return false;

  // Compare the distortion model (if distortion is set for both).
  if (distortion_ && rhs->distortion_) {
    if ( !(*(this->distortion_) == *(rhs->distortion_)) )
      return false;
  }

  return true;
}

bool UnifiedProjectionCamera::backProject3(const Eigen::Vector2d& keypoint,
                                           Eigen::Vector3d* out_point_3d) const {
  CHECK_NOTNULL(out_point_3d);

  Eigen::Vector2d kp = keypoint;
  kp[0] = (kp[0] - cu()) / fu();
  kp[1] = (kp[1] - cv()) / fv();

  if(distortion_)
    distortion_->undistort(&kp);

  const double rho2_d = kp[0] * kp[0] + kp[1] * kp[1];
  const double tmpD = std::max(1 + (1 - xi()*xi()) * rho2_d, 0.0);

  (*out_point_3d)[0] = kp[0];
  (*out_point_3d)[1] = kp[1];
  (*out_point_3d)[2] = 1 - xi() * (rho2_d + 1) / (xi() + sqrt(tmpD));

  return isUndistortedKeypointValid(rho2_d, xi());
}

const ProjectionResult UnifiedProjectionCamera::project3Functional(
    const Eigen::Vector3d& point_3d,
    const Eigen::VectorXd* intrinsics_external,
    const Eigen::VectorXd* distortion_coefficients_external,
    Eigen::Vector2d* out_keypoint,
    Eigen::Matrix<double, 2, 3>* out_jacobian_point,
    Eigen::Matrix<double, 2, Eigen::Dynamic>* out_jacobian_intrinsics,
    Eigen::Matrix<double, 2, Eigen::Dynamic>* out_jacobian_distortion) const {
  CHECK_NOTNULL(out_keypoint);

  // Determine the parameter source. (if nullptr, use internal)
  const Eigen::VectorXd* intrinsics;
  if (!intrinsics_external)
    intrinsics = &getParameters();
  else
    intrinsics = intrinsics_external;
  CHECK_EQ(intrinsics->size(), kNumOfParams) << "intrinsics: invalid size!";

  const Eigen::VectorXd* distortion_coefficients;
  if(!distortion_coefficients_external && distortion_) {
    distortion_coefficients = &getDistortion()->getParameters();
  } else {
    distortion_coefficients = distortion_coefficients_external;
  }

  const double& xi = (*intrinsics)[0];
  const double& fu = (*intrinsics)[1];
  const double& fv = (*intrinsics)[2];
  const double& cu = (*intrinsics)[3];
  const double& cv = (*intrinsics)[4];

  // Project the point.
  const double& x = point_3d[0];
  const double& y = point_3d[1];
  const double& z = point_3d[2];

  const double d = point_3d.norm();
  const double rz = 1.0 / (z + xi * d);

  // Check if point will lead to a valid projection
  const bool valid_proj = z > -(fov_parameter(xi) * d);
  if(!valid_proj)
  {
    out_keypoint->setZero();
    return ProjectionResult(ProjectionResult::Status::PROJECTION_INVALID);
  }

  (*out_keypoint)[0] = x * rz;
  (*out_keypoint)[1] = y * rz;

  // Distort the point and get the Jacobian wrt. keypoint.
  Eigen::Matrix2d J_distortion = Eigen::Matrix2d::Identity();
  if (distortion_ && out_jacobian_point) {
    // Distortion active and we want the Jacobian.
    distortion_->distortUsingExternalCoefficients(distortion_coefficients,
                                                  out_keypoint,
                                                  &J_distortion);
  } else if (distortion_) {
    // Distortion active but Jacobian NOT wanted.
    distortion_->distortUsingExternalCoefficients(distortion_coefficients,
                                                  out_keypoint,
                                                  nullptr);
  }

  // Calculate the Jacobian w.r.t to the 3d point, if requested.
  if(out_jacobian_point) {
    // Jacobian including distortion
    Eigen::Matrix<double, 2, 3>& J = *out_jacobian_point;
    double rz2 = rz * rz / d;
    J(0, 0) = rz2 * (d * z + xi * (y * y + z * z));
    J(1, 0) = -rz2 * xi * x * y;
    J(0, 1) = J(1, 0);
    J(1, 1) = rz2 * (d * z + xi * (x * x + z * z));
    rz2 = rz2 * (-xi * z - d);
    J(0, 2) = x * rz2;
    J(1, 2) = y * rz2;
    rz2 = fu * (J(0, 0) * J_distortion(0, 0) + J(1, 0) * J_distortion(0, 1));
    J(1, 0) = fv * (J(0, 0) * J_distortion(1, 0) + J(1, 0) * J_distortion(1, 1));
    J(0, 0) = rz2;
    rz2 = fu * (J(0, 1) * J_distortion(0, 0) + J(1, 1) * J_distortion(0, 1));
    J(1, 1) = fv * (J(0, 1) * J_distortion(1, 0) + J(1, 1) * J_distortion(1, 1));
    J(0, 1) = rz2;
    rz2 = fu * (J(0, 2) * J_distortion(0, 0) + J(1, 2) * J_distortion(0, 1));
    J(1, 2) = fv * (J(0, 2) * J_distortion(1, 0) + J(1, 2) * J_distortion(1, 1));
    J(0, 2) = rz2;
  }

  // Calculate the Jacobian w.r.t to the intrinsic parameters, if requested.
  if(out_jacobian_intrinsics) {
    out_jacobian_intrinsics->resize(2, kNumOfParams);
    out_jacobian_intrinsics->setZero();

    Eigen::Vector2d Jxi;
    Jxi[0] = -(*out_keypoint)[0] * d * rz;
    Jxi[1] = -(*out_keypoint)[1] * d * rz;
    J_distortion.row(0) *= fu;
    J_distortion.row(1) *= fv;
    (*out_jacobian_intrinsics).col(0) = J_distortion * Jxi;

    (*out_jacobian_intrinsics)(0, 1) = (*out_keypoint)[0];
    (*out_jacobian_intrinsics)(0, 3) = 1;
    (*out_jacobian_intrinsics)(1, 2) = (*out_keypoint)[1];
    (*out_jacobian_intrinsics)(1, 4) = 1;
  }

  // Calculate the Jacobian w.r.t to the distortion parameters, if requested (and distortion set)
  if(distortion_ && out_jacobian_distortion) {
    distortion_->distortParameterJacobian(distortion_coefficients_external,
                                          *out_keypoint,
                                          out_jacobian_distortion);
    (*out_jacobian_distortion).row(0) *= fu;
    (*out_jacobian_distortion).row(1) *= fv;
  }

  // Normalized image plane to camera plane.
  (*out_keypoint)[0] = fu * (*out_keypoint)[0] + cu;
  (*out_keypoint)[1] = fv * (*out_keypoint)[1] + cv;

  return evaluateProjectionResult(*out_keypoint, point_3d);
}

inline const ProjectionResult UnifiedProjectionCamera::evaluateProjectionResult(
    const Eigen::Vector2d& keypoint,
    const Eigen::Vector3d& point_3d) const {

  const bool visibility = isKeypointVisible(keypoint);

  const double d2 = point_3d.squaredNorm();
  const double minDepth2 = kMinimumDepth*kMinimumDepth;

  if (visibility && (d2 > minDepth2))
    return ProjectionResult(ProjectionResult::Status::KEYPOINT_VISIBLE);
  else if (!visibility && (d2 > minDepth2))
    return ProjectionResult(ProjectionResult::Status::KEYPOINT_OUTSIDE_IMAGE_BOX);
  else if (d2 <= minDepth2)
    return ProjectionResult(ProjectionResult::Status::PROJECTION_INVALID);

  return ProjectionResult(ProjectionResult::Status::PROJECTION_INVALID);
}

inline bool UnifiedProjectionCamera::isUndistortedKeypointValid(const double& rho2_d,
                                                   const double& xi) const {
  return xi <= 1.0 || rho2_d <= (1.0 / (xi * xi - 1));
}

bool UnifiedProjectionCamera::isLiftable(const Eigen::Vector2d& keypoint) const {
  Eigen::Vector2d y;
  y[0] = 1.0/fu() * (keypoint[0] - cu());
  y[1] = 1.0/fv() * (keypoint[1] - cv());

  if(distortion_)
    distortion_->undistort(&y);

  // Now check if it is on the sensor
  double rho2_d = y[0] * y[0] + y[1] * y[1];
  return isUndistortedKeypointValid(rho2_d, xi());
}

Eigen::Vector2d UnifiedProjectionCamera::createRandomKeypoint() const {
  // This is tricky...The camera model defines a circle on the normalized image
  // plane and the projection equations don't work outside of it.
  // With some manipulation, we can see that, on the normalized image plane,
  // the edge of this circle is at u^2 + v^2 = 1/(xi^2 - 1)
  // So: this function creates keypoints inside this boundary.


  // Create a point on the normalized image plane inside the boundary.
  // This is not efficient, but it should be correct.
  const double ru = imageWidth(),
               rv = imageHeight();

  Eigen::Vector2d u(ru + 1, rv + 1);
  double one_over_xixi_m_1 = 1.0 / (xi() * xi() - 1);

  int max_tries = 10;
  while ( !(isLiftable(u) && isKeypointVisible(u)) ) {
    u.setRandom();
    u = u - Eigen::Vector2d(0.5, 0.5);
    u /= u.norm();
    u *= ((double) rand() / (double) RAND_MAX) * one_over_xixi_m_1;

    // Now we run the point through distortion and projection.
    // Apply distortion
    if(distortion_)
      distortion_->distort(&u);

    u[0] = fu() * u[0] + cu();
    u[1] = fv() * u[1] + cv();

    // Protect against inifinte loops.
    if(--max_tries<1)
    {
      u << cu(), cv();  //image center
      VLOG(2) << "UnifiedProjectionCamera::createRandomKeypoint "
              << "failed to produce a random keypoint!";
      break;
    }
  }
  return u;
}

Eigen::Vector3d UnifiedProjectionCamera::createRandomVisiblePoint(double depth) const {
  CHECK_GT(depth, 0.0) << "Depth needs to be positive!";


  Eigen::Vector2d y = createRandomKeypoint();

  Eigen::Vector3d point_3d;
  bool success = backProject3(y, &point_3d);
  CHECK(success) << "backprojection of createRandomVisiblePoint was unsuccessful!";
  point_3d /= point_3d.norm();

  // Muck with the depth. This doesn't change the pointing direction.
  return point_3d * depth;
}

std::unique_ptr<MappedUndistorter> UnifiedProjectionCamera::createMappedUndistorter(
    float alpha, float scale, aslam::InterpolationMethod interpolation_type) const {

  CHECK_GE(alpha, 0.0); CHECK_LE(alpha, 1.0);
  CHECK_GT(scale, 0.0);

  // Only remove distortion effects.
  const bool undistort_to_pinhole = false;

  // Create a copy of the input camera (=this)
  UnifiedProjectionCamera::Ptr input_camera(dynamic_cast<UnifiedProjectionCamera*>(this->clone()));
  CHECK(input_camera);

  // Create the scaled output camera with removed distortion.
  Eigen::Matrix3d output_camera_matrix = common::getOptimalNewCameraMatrix(*input_camera, alpha,
                                                                           scale,
                                                                           undistort_to_pinhole);

  Eigen::Matrix<double, parameterCount(), 1> intrinsics;
  intrinsics <<  xi(), output_camera_matrix(0, 0), output_camera_matrix(1, 1),
                       output_camera_matrix(0, 2), output_camera_matrix(1, 2);

  const int output_width = static_cast<int>(scale * imageWidth());
  const int output_height = static_cast<int>(scale * imageHeight());
  UnifiedProjectionCamera::Ptr output_camera = aslam::createCamera<aslam::UnifiedProjectionCamera>(
      intrinsics, output_width, output_height);
  CHECK(output_camera);

  cv::Mat map_u, map_v;
  aslam::common::buildUndistortMap(*input_camera, *output_camera, undistort_to_pinhole, CV_16SC2,
                                   map_u, map_v);

  return std::unique_ptr<MappedUndistorter>(
      new MappedUndistorter(input_camera, output_camera, map_u, map_v, interpolation_type));
}

std::unique_ptr<MappedUndistorter> UnifiedProjectionCamera::createMappedUndistorterToPinhole(
    float alpha, float scale, aslam::InterpolationMethod interpolation_type) const {

  CHECK_GE(alpha, 0.0); CHECK_LE(alpha, 1.0);
  CHECK_GT(scale, 0.0);

  // Undistort to pinhole view.
  const bool undistort_to_pinhole = true;

  // Create a copy of the input camera (=this)
  UnifiedProjectionCamera::Ptr input_camera(dynamic_cast<UnifiedProjectionCamera*>(this->clone()));
  CHECK(input_camera);

  // Create the scaled output camera with removed distortion.
  Eigen::Matrix3d output_camera_matrix = aslam::common::getOptimalNewCameraMatrix(
      *input_camera, alpha, scale, undistort_to_pinhole);

  Eigen::Matrix<double, PinholeCamera::parameterCount(), 1> intrinsics;
  intrinsics <<  output_camera_matrix(0, 0), output_camera_matrix(1, 1),
                 output_camera_matrix(0, 2), output_camera_matrix(1, 2);

  const int output_width = static_cast<int>(scale * imageWidth());
  const int output_height = static_cast<int>(scale * imageHeight());
  PinholeCamera::Ptr output_camera = aslam::createCamera<aslam::PinholeCamera>(
      intrinsics, output_width, output_height);
  CHECK(output_camera);

  cv::Mat map_u, map_v;
  aslam::common::buildUndistortMap(*input_camera, *output_camera, undistort_to_pinhole, CV_16SC2,
                                   map_u, map_v);

  return std::unique_ptr<MappedUndistorter>(
      new MappedUndistorter(input_camera, output_camera, map_u, map_v, interpolation_type));
}

bool UnifiedProjectionCamera::intrinsicsValid(const Eigen::VectorXd& intrinsics) {
  return (intrinsics.size() == parameterCount()) &&
         (intrinsics[0] >= 0.0) && //xi
         (intrinsics[1] > 0.0)  && //fu
         (intrinsics[2] > 0.0)  && //fv
         (intrinsics[3] > 0.0)  && //cu
         (intrinsics[4] > 0.0);    //cv
}

void UnifiedProjectionCamera::printParameters(std::ostream& out, const std::string& text) const {
  Camera::printParameters(out, text);
  out << "  mirror parameter (xi): "
      << xi() << std::endl;
  out << "  focal length (cols,rows): "
      << fu() << ", " << fv() << std::endl;
  out << "  optical center (cols,rows): "
      << cu() << ", " << cv() << std::endl;

  if(distortion_) {
    out << "  distortion: ";
    distortion_->printParameters(out, text);
  }
}
}  // namespace aslam
