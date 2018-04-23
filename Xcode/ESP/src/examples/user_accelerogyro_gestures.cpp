/** @example user_accelerometer_gestures.cpp
 * Gesture detection using accelerometers. See the <a href="https://create.arduino.cc/projecthub/mellis/gesture-recognition-using-accelerometer-and-esp-mac-only-71faa1">associated tutorial</a>.
 */
#include <ESP.h>

ASCIISerialStream stream(115200, 6);
GestureRecognitionPipeline pipeline;
Calibrator calibrator;
TcpOStream oStream("localhost", 5204);

//std::map<uint32_t, char> key_mapping = { {1, 0}, {2, 0}, {3, ' '} };
//std::map<uint32_t, uint16_t> key_code_mapping = { {1, 0x7C}, {2, 0x7B} };
//MacOSKeyboardOStream oStream(key_mapping, key_code_mapping);
//MacOSKeyboardOStream oStream(3, 0, 0x7C, 0, 0x7B, ' '); // right, left, space

MatrixDouble uprightData, upsideDownData;
bool haveUprightData = false, haveUpsideDownData = false;
double range;
vector<double> zeroGs(6);
vector<double> processAccelerometerData(vector<double> input)
{
    vector<double> result(6);

    for (int i = 0; i < 6; i++) {
        result[i] = (input[i] - zeroGs[i]) / range;
    }

    return result;
}

CalibrateResult calibrate(const MatrixDouble& data) {
    CalibrateResult result = CalibrateResult::SUCCESS;

    // Run checks on newly collected sample.

    // take average of X and Y acceleration as the zero G value
    double zG = (data.getMean()[0] + data.getMean()[1]) / 2;
    double oG = data.getMean()[2]; // use Z acceleration as one G value

    // take average of X and Y gyro as the zero G value;
    double zGY = (data.getMean()[3] + data.getMean()[4]) / 2;
    double oGY = data.getMean()[5]; // use Z gyro as one G value;

    double r = abs(oG - zG);
    double rG = abs(oGY - zGY);
    vector<double> stddev = data.getStdDev();

    if ((abs(data.getMean()[0] - data.getMean()[1]) / r > 0.1) || (abs(data.getMean()[3] - data.getMean()[4]) / rG > 0.1))
        result = CalibrateResult(CalibrateResult::WARNING,
            "X and Y axes differ by " + std::to_string(
            abs(data.getMean()[0] - data.getMean()[1]) / r * 100) +
            " percent. Check that accelerometer is flat.");

    if (stddev[0] / r > 0.05 ||
        stddev[1] / r > 0.05 ||
        stddev[2] / r > 0.05 ||
        stddev[3] / r > 0.05 ||
        stddev[4] / r > 0.05 ||
        stddev[5] / r > 0.05)
        result = CalibrateResult(CalibrateResult::WARNING,
            "Accelerometer/Gyro data is noisy. Check circuit.");

    // If we have both samples, do the actual calibration.

    if (haveUprightData && haveUpsideDownData) {
        for (int i = 0; i < 6; i++) {
            zeroGs[i] =
                (uprightData.getMean()[i] + upsideDownData.getMean()[i]) / 2;
        }

        // use half the difference between the two z-axis values (-1 and +1)
        // as the range
        range = (uprightData.getMean()[2] - upsideDownData.getMean()[2]) / 2;
    }

    return result;
}

CalibrateResult uprightDataCollected(const MatrixDouble& data)
{
    uprightData = data;
    haveUprightData = true;
    return calibrate(data);
}

CalibrateResult upsideDownDataCollected(const MatrixDouble& data)
{
    upsideDownData = data;
    haveUpsideDownData = true;
    return calibrate(data);
}

TrainingSampleCheckerResult checkTrainingSample(const MatrixDouble &in)
{
    if (in.getNumRows() < 10)
        return TrainingSampleCheckerResult(TrainingSampleCheckerResult::WARNING,
            "Warning: Sample is short. Did you hold down the key for "
            "the whole time you were making the gesture?");
    VectorDouble stddev = in.getStdDev();
    if (*max_element(stddev.begin(), stddev.end()) < 0.1)
        return TrainingSampleCheckerResult(TrainingSampleCheckerResult::WARNING,
            "Warning: Gesture contains very little movement.");
    return TrainingSampleCheckerResult::SUCCESS;
}

int timeout = 100; // milliseconds
double null_rej = 0.2;

void updateVariability(double new_null_rej) {
    pipeline.getClassifier()->setNullRejectionCoeff(new_null_rej);
    pipeline.getClassifier()->recomputeNullRejectionThresholds();
}

void updateTimeout(int new_timeout) {
    ClassLabelTimeoutFilter *filter =
        dynamic_cast<ClassLabelTimeoutFilter *>
            (pipeline.getPostProcessingModule(0));
    assert(filter != nullptr);
    filter->setTimeoutDuration(new_timeout);
}

void setup()
{
    stream.setLabelsForAllDimensions({"aX", "aY", "aZ", "gX", "gY", "gZ"});
    useStream(stream);
    useOutputStream(oStream);

    calibrator.setCalibrateFunction(processAccelerometerData);
    calibrator.addCalibrateProcess("Upright",
        "Rest accelerometer upright on flat surface.", uprightDataCollected);
    calibrator.addCalibrateProcess("Upside Down",
        "Rest accelerometer upside down on flat surface.", upsideDownDataCollected);
    useCalibrator(calibrator);

    DTW dtw(false, true, null_rej);
    dtw.enableTrimTrainingData(true, 0.1, 75);

    pipeline.setClassifier(dtw);
    pipeline.addPostProcessingModule(ClassLabelTimeoutFilter(timeout));
    usePipeline(pipeline);

    registerTuneable(null_rej, 0.1, 5.0, "Variability",
         "How different from the training data a new gesture can be and "
         "still be considered the same gesture. The higher the number, the "
         "more different it can be.", updateVariability);
    registerTuneable(timeout, 1, 3000,
        "Timeout",
        "How long (in milliseconds) to wait after recognizing a "
        "gesture before recognizing another one.", updateTimeout);

    useTrainingSampleChecker(checkTrainingSample);
  
    setTruePositiveWarningThreshold(0.60);
    setFalseNegativeWarningThreshold(0.30);
}
