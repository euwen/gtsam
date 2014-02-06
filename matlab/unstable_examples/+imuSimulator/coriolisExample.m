%% coriolisExample.m
% Author(s): David Jensen (david.jensen@gtri.gatech.edu)
% This script demonstrates the relationship between object motion in inertial and rotating reference frames.
% For this example, we consider a fixed (inertial) reference frame located at the center of the earth and initially
% coincident with the rotating ECEF reference frame (X towards 0 longitude, Y towards 90 longitude, Z towards 90 latitude),
% which rotates with the earth.
% A body is moving in the positive Z-direction and positive Y-direction with respect to the fixed reference frame.
% Its position is plotted in both the fixed and rotating reference frames to simulate how an observer in each frame would
% experience the body's motion.

clc
clear all
close all

import gtsam.*;

addpath(genpath('./Libraries'))

%% General configuration
deltaT = 0.1;
timeElapsed = 10;
times = 0:deltaT:timeElapsed;

%omega = [0;0;7.292115e-5]; % Earth Rotation
omega = [0;0;pi/30];
omega = [0;0;0];
omegaFixed = [0;0;0];
velocity = [0;0;0];                 % initially not moving
accelFixed = [0.1;0.1;0.1];           % accelerate in the positive z-direction
initialPosition = [0; 1.05; 0];     % start along the positive x-axis
IMUinBody = Pose3;
g = [0;0;0];                        % Gravity
zeroBias = imuBias.ConstantBias(zeros(3,1), zeros(3,1));
IMU_metadata.AccelerometerSigma = 1e-5;
IMU_metadata.GyroscopeSigma = 1e-7;
IMU_metadata.IntegrationSigma = 1e-10;
sigma_init_x = noiseModel.Isotropic.Sigma(6, 1e-10);
sigma_init_v = noiseModel.Isotropic.Sigma(3, 1e-10);
sigma_init_b = noiseModel.Isotropic.Sigma(6, 1e-10);

%% Initial state of the body in the fixed in rotating frames should be the same
currentPoseFixedGT = Pose3(Rot3, Point3(initialPosition));
currentVelocityFixedGT = velocity;

currentPoseRotatingGT = currentPoseFixedGT;
currentPoseRotatingFrame = Pose3;

%% Initialize storage variables
positionsFixedGT = zeros(3, length(times));
positionsRotatingGT = zeros(3, length(times));
positionsEstimates = zeros(3,length(times));

changePoseRotatingFrame = Pose3.Expmap([omega*deltaT; 0; 0; 0]);
h = figure(1);

% Solver object
isamParams = ISAM2Params;
isamParams.setFactorization('CHOLESKY');
isamParams.setRelinearizeSkip(10);
isam = gtsam.ISAM2(isamParams);
newFactors = NonlinearFactorGraph;
newValues = Values;


%% Main loop: iterate through the ground truth trajectory, add factors
% and values to the factor graph, and perform inference
for i = 1:length(times)
    t = times(i);
    
    % Create keys for current state
    currentPoseKey = symbol('x', i);
    currentVelKey = symbol('v', i);
    currentBiasKey = symbol('b', i);
    
    %% Set priors on the first iteration
    if(i == 1)
        positionsFixedGT(:,1) = currentPoseFixedGT.translation.vector;
        positionsRotatingGT(:,1) = currentPoseRotatingGT.translation.vector;
        poses(1).p = currentPoseRotatingFrame.translation.vector;
        poses(1).R = currentPoseRotatingFrame.rotation.matrix;
        
        currentPoseEstimate = currentPoseFixedGT;
        currentVelocityEstimate = LieVector(currentVelocityFixedGT);

        % Set Priors
        newValues.insert(currentPoseKey, currentPoseEstimate);
        newValues.insert(currentVelKey, currentVelocityEstimate);
        newValues.insert(currentBiasKey, zeroBias);
        newFactors.add(PriorFactorPose3(currentPoseKey, currentPoseEstimate, sigma_init_x));
        newFactors.add(PriorFactorLieVector(currentVelKey, currentVelocityEstimate, sigma_init_v));
        newFactors.add(PriorFactorConstantBias(currentBiasKey, zeroBias, sigma_init_b));
        
        % Store data
        positionsEstimates(:,i) = currentPoseEstimate.translation.vector;
        
    else
        
        %% Create ground truth trajectory
        % Update the position and velocity
        % x_t = x_0 + v_0*dt + 1/2*a_0*dt^2
        % v_t = v_0 + a_0*dt
        currentPositionFixedGT = Point3(currentPoseFixedGT.translation.vector ...
            + currentVelocityFixedGT * deltaT + 0.5 * accelFixed * deltaT * deltaT);
        currentVelocityFixedGT = currentVelocityFixedGT + accelFixed * deltaT;
        
        currentPoseFixedGT = Pose3(Rot3, currentPositionFixedGT);
        
        % Rotate pose in fixed frame to get pose in rotating frame
        currentPoseRotatingFrame = currentPoseRotatingFrame.compose(changePoseRotatingFrame);
        currentPoseRotatingGT = currentPoseFixedGT.transform_to(currentPoseRotatingFrame);
        
        % Store GT (ground truth) poses
        positionsFixedGT(:,i) = currentPoseFixedGT.translation.vector;
        positionsRotatingGT(:,i) = currentPoseRotatingGT.translation.vector;
        poses(i).p = currentPoseRotatingFrame.translation.vector;
        poses(i).R = currentPoseRotatingFrame.rotation.matrix;
        
        %% Estimate trajectory in rotating frame using the ground truth measurements
        % Instantiate preintegrated measurements class
        currentSummarizedMeasurement = gtsam.ImuFactorPreintegratedMeasurements( ...
            zeroBias, IMU_metadata.AccelerometerSigma.^2 * eye(3), ...
            IMU_metadata.GyroscopeSigma.^2 * eye(3), IMU_metadata.IntegrationSigma.^2 * eye(3));
        % Add measurement
        currentSummarizedMeasurement.integrateMeasurement(accelFixed, omegaFixed, deltaT);
        % Add factors to graph
        newFactors.add(ImuFactor( ...
            currentPoseKey-1, currentVelKey-1, ...
            currentPoseKey, currentVelKey, ...
            currentBiasKey-1, currentSummarizedMeasurement, g, omega));
        
        %newFactors.add(PriorFactorConstantBias(currentBiasKey, zeroBias, sigma_init_b));
        newFactors.add(BetweenFactorConstantBias(currentBiasKey-1, currentBiasKey, imuBias.ConstantBias(zeros(3,1), zeros(3,1)), ...
            noiseModel.Isotropic.Sigma(6, 1e-10)));
        
        % Add values to the graph. Use the current pose and velocity
        % estimates as to values when interpreting the next pose and
        % velocity estimates
        newValues.insert(currentPoseKey, currentPoseEstimate);
        newValues.insert(currentVelKey, currentVelocityEstimate);
        newValues.insert(currentBiasKey, zeroBias);
        
        %newFactors.print('');
        %newValues.print('');
        
        %% Solve factor graph
        if(i > 1)
            isam.update(newFactors, newValues);
            newFactors = NonlinearFactorGraph;
            newValues = Values;
            
            % Get the new pose, velocity, and bias estimates
            currentPoseEstimate = isam.calculateEstimate(currentPoseKey);
            currentVelocityEstimate = isam.calculateEstimate(currentVelKey);
            currentBias = isam.calculateEstimate(currentBiasKey);
            
            positionsEstimates(:,i) = currentPoseEstimate.translation.vector;
            %velocitiesEstimates(:,i) = currentVelocityGlobal;
        end
    end
    
    %% incremental plotting for animation (ground truth)
    figure(h)
    plot_trajectory(poses(i),1, '-k', 'Rotating Frame',0.1,0.75,1)
    hold on;
    plot3(positionsFixedGT(1,1:i), positionsFixedGT(2,1:i), positionsFixedGT(3,1:i));
    plot3(positionsFixedGT(1,1), positionsFixedGT(2,1), positionsFixedGT(3,1), 'x');
    plot3(positionsFixedGT(1,i), positionsFixedGT(2,i), positionsFixedGT(3,i), 'o');
    
    plot3(positionsRotatingGT(1,1:i), positionsRotatingGT(2,1:i), positionsRotatingGT(3,1:i), '-r');
    plot3(positionsRotatingGT(1,1), positionsRotatingGT(2,1), positionsRotatingGT(3,1), 'xr');
    plot3(positionsRotatingGT(1,i), positionsRotatingGT(2,i), positionsRotatingGT(3,i), 'or');
    
    plot3(positionsEstimates(1,1:i), positionsEstimates(2,1:i), positionsEstimates(3,1:i), '-g');
    plot3(positionsEstimates(1,1), positionsEstimates(2,1), positionsEstimates(3,1), 'xg');
    plot3(positionsEstimates(1,i), positionsEstimates(2,i), positionsEstimates(3,i), 'og');
    
    hold off;
    xlabel('X axis')
    ylabel('Y axis')
    zlabel('Z axis')
    axis equal
    grid on;
    %pause(0.1);
    
    i = i + 1;
end

figure
%% Print and plot trajectory error results
positionsError = positionsRotatingGT - positionsEstimates;
fprintf('Final position error = %f\n', positionsError(end));
plot(times, positionsError);
plotTitle = sprintf('Error in Estimated Position (omega = [%.2f; %.2f; %.2f])', omega(1), omega(2), omega(3));
title(plotTitle);
xlabel('Time');
ylabel('Error (ground_truth - estimate)');
legend('X axis', 'Y axis', 'Z axis', 'Location', 'NORTHWEST');

%% Plot final trajectories
figure
sphere  % sphere for reference
hold on;
% Ground truth trajectory in fixed reference frame
plot3(positionsFixedGT(1,:), positionsFixedGT(2,:), positionsFixedGT(3,:));
plot3(positionsFixedGT(1,1), positionsFixedGT(2,1), positionsFixedGT(3,1), 'x');
plot3(positionsFixedGT(1,end), positionsFixedGT(2,end), positionsFixedGT(3,end), 'o');

% Ground truth trajectory in rotating reference frame
plot3(positionsRotatingGT(1,:), positionsRotatingGT(2,:), positionsRotatingGT(3,:), '-r');
plot3(positionsRotatingGT(1,1), positionsRotatingGT(2,1), positionsRotatingGT(3,1), 'xr');
plot3(positionsRotatingGT(1,end), positionsRotatingGT(2,end), positionsRotatingGT(3,end), 'or');

% Estimates
plot3(positionsEstimates(1,:), positionsEstimates(2,:), positionsEstimates(3,:), '-g');
plot3(positionsEstimates(1,1), positionsEstimates(2,1), positionsEstimates(3,1), 'xg');
plot3(positionsEstimates(1,end), positionsEstimates(2,end), positionsEstimates(3,end), 'og');

xlabel('X axis')
ylabel('Y axis')
zlabel('Z axis')
axis equal
grid on;
hold off;