#include <vector>

#include "envoy/config/core/v3/base.pb.h"
#include "envoy/http/filter.h"

#include "source/extensions/filters/http/aws_lambda/request_response.pb.validate.h"

#include "extensions/filters/http/aws_lambda/aws_lambda_filter.h"
#include "extensions/filters/http/well_known_names.h"

#include "test/extensions/common/aws/mocks.h"
#include "test/mocks/http/mocks.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace AwsLambdaFilter {

namespace {

using Common::Aws::MockSigner;
using ::testing::An;
using ::testing::ElementsAre;
using ::testing::InSequence;
using ::testing::Invoke;
using ::testing::Pair;
using ::testing::Return;
using ::testing::ReturnRef;
using ::testing::UnorderedElementsAre;

constexpr auto Arn = "arn:aws:lambda:us-west-2:1337:function:fun";
class AwsLambdaFilterTest : public ::testing::Test {
public:
  void setupFilter(const FilterSettings& settings) {
    signer_ = std::make_shared<NiceMock<MockSigner>>();
    filter_ = std::make_unique<Filter>(settings, signer_);
    filter_->setDecoderFilterCallbacks(decoder_callbacks_);
    filter_->setEncoderFilterCallbacks(encoder_callbacks_);
    const std::string metadata_yaml = "egress_gateway: true";

    ProtobufWkt::Struct cluster_metadata;
    TestUtility::loadFromYaml(metadata_yaml, cluster_metadata);
    metadata_.mutable_filter_metadata()->insert({"com.amazonaws.lambda", cluster_metadata});
    ON_CALL(*decoder_callbacks_.cluster_info_, metadata()).WillByDefault(ReturnRef(metadata_));
  }

  std::unique_ptr<Filter> filter_;
  std::shared_ptr<NiceMock<MockSigner>> signer_;
  NiceMock<Http::MockStreamDecoderFilterCallbacks> decoder_callbacks_;
  NiceMock<Http::MockStreamEncoderFilterCallbacks> encoder_callbacks_;
  envoy::config::core::v3::Metadata metadata_;
};

/**
 * Requests that are _not_ header only, should result in StopIteration.
 */
TEST_F(AwsLambdaFilterTest, DecodingHeaderStopIteration) {
  setupFilter({Arn, InvocationMode::Synchronous, true /*passthrough*/});
  Http::TestRequestHeaderMapImpl headers;
  const auto result = filter_->decodeHeaders(headers, false /*end_stream*/);
  EXPECT_EQ(Http::FilterHeadersStatus::StopIteration, result);
}

/**
 * Header only pass-through requests should be signed and Continue iteration.
 */
TEST_F(AwsLambdaFilterTest, HeaderOnlyShouldContinue) {
  setupFilter({Arn, InvocationMode::Synchronous, true /*passthrough*/});
  EXPECT_CALL(*signer_, sign(_));
  Http::TestRequestHeaderMapImpl input_headers;
  const auto result = filter_->decodeHeaders(input_headers, true /*end_stream*/);
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, result);

  Http::TestResponseHeaderMapImpl response_headers;
  const auto encode_result = filter_->encodeHeaders(response_headers, true /*end_stream*/);
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, encode_result);
}

/**
 * If the filter is configured with an invalid ARN, then we stop.
 */
TEST_F(AwsLambdaFilterTest, ConfigurationWithInvalidARN) {
  setupFilter({"BadARN", InvocationMode::Synchronous, true /*passthrough*/});
  EXPECT_CALL(decoder_callbacks_, sendLocalReply);
  Http::TestRequestHeaderMapImpl headers;
  const auto result = filter_->decodeHeaders(headers, true /*end_stream*/);
  EXPECT_EQ(Http::FilterHeadersStatus::StopIteration, result);
}

/**
 * If there's a per-route configuration with an invalid ARN, then we stop.
 */
TEST_F(AwsLambdaFilterTest, PerRouteConfigWithInvalidARN) {
  setupFilter({Arn, InvocationMode::Synchronous, true /*passthrough*/});
  FilterSettings route_settings{"BadARN", InvocationMode::Synchronous, true /*passthrough*/};
  ON_CALL(decoder_callbacks_.route_->route_entry_,
          perFilterConfig(HttpFilterNames::get().AwsLambda))
      .WillByDefault(Return(&route_settings));
  EXPECT_CALL(decoder_callbacks_, sendLocalReply);
  Http::TestRequestHeaderMapImpl headers;
  const auto result = filter_->decodeHeaders(headers, true /*end_stream*/);
  EXPECT_EQ(Http::FilterHeadersStatus::StopIteration, result);
}

/**
 * If there's a per route config and the target cluster has the _wrong_ metadata, then skip the
 * filter.
 */
TEST_F(AwsLambdaFilterTest, PerRouteConfigWrongClusterMetadata) {
  const std::string metadata_yaml = R"EOF(
  egress_gateway: true
  )EOF";

  ProtobufWkt::Struct cluster_metadata;
  envoy::config::core::v3::Metadata metadata;
  TestUtility::loadFromYaml(metadata_yaml, cluster_metadata);
  metadata.mutable_filter_metadata()->insert({"WrongMetadataKey", cluster_metadata});

  setupFilter({Arn, InvocationMode::Synchronous, true /*passthrough*/});
  FilterSettings route_settings{Arn, InvocationMode::Synchronous, true /*passthrough*/};
  ON_CALL(decoder_callbacks_.route_->route_entry_,
          perFilterConfig(HttpFilterNames::get().AwsLambda))
      .WillByDefault(Return(&route_settings));

  ON_CALL(*decoder_callbacks_.cluster_info_, metadata()).WillByDefault(ReturnRef(metadata));
  Http::TestRequestHeaderMapImpl headers;

  const auto decode_header_result = filter_->decodeHeaders(headers, false /*end_stream*/);
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, decode_header_result);

  Buffer::OwnedImpl buf;
  const auto decode_data_result = filter_->decodeData(buf, true /*end_stream*/);
  EXPECT_EQ(Http::FilterDataStatus::Continue, decode_data_result);
  EXPECT_EQ(0, buf.length());
}

/**
 * If there's a per route config and the target cluster has the _correct_ metadata, then we should
 * process the request (i.e. StopIteration if end_stream is false)
 */
TEST_F(AwsLambdaFilterTest, PerRouteConfigCorrectClusterMetadata) {
  setupFilter({Arn, InvocationMode::Synchronous, true /*passthrough*/});
  FilterSettings route_settings{Arn, InvocationMode::Synchronous, true /*passthrough*/};
  ON_CALL(decoder_callbacks_.route_->route_entry_,
          perFilterConfig(HttpFilterNames::get().AwsLambda))
      .WillByDefault(Return(&route_settings));

  Http::TestRequestHeaderMapImpl headers;
  const auto result = filter_->decodeHeaders(headers, false /*end_stream*/);
  EXPECT_EQ(Http::FilterHeadersStatus::StopIteration, result);
}

TEST_F(AwsLambdaFilterTest, DecodeDataShouldBuffer) {
  setupFilter({Arn, InvocationMode::Synchronous, true /*passthrough*/});
  Http::TestRequestHeaderMapImpl headers;
  const auto header_result = filter_->decodeHeaders(headers, false /*end_stream*/);
  EXPECT_EQ(Http::FilterHeadersStatus::StopIteration, header_result);
  Buffer::OwnedImpl buffer;
  const auto data_result = filter_->decodeData(buffer, false);
  EXPECT_EQ(Http::FilterDataStatus::StopIterationAndBuffer, data_result);
}

TEST_F(AwsLambdaFilterTest, DecodeDataShouldSign) {
  setupFilter({Arn, InvocationMode::Synchronous, true /*passthrough*/});
  Http::TestRequestHeaderMapImpl headers;
  const auto header_result = filter_->decodeHeaders(headers, false /*end_stream*/);
  EXPECT_EQ(Http::FilterHeadersStatus::StopIteration, header_result);
  Buffer::OwnedImpl buffer;

  InSequence seq;
  EXPECT_CALL(decoder_callbacks_, addDecodedData(_, false)).Times(1);
  EXPECT_CALL(decoder_callbacks_, decodingBuffer).WillOnce(Return(&buffer));
  EXPECT_CALL(*signer_, sign(An<Http::RequestHeaderMap&>(), An<const std::string&>())).Times(1);

  const auto data_result = filter_->decodeData(buffer, true /*end_stream*/);
  EXPECT_EQ(Http::FilterDataStatus::Continue, data_result);
}

TEST_F(AwsLambdaFilterTest, DecodeHeadersInvocationModeSetsHeader) {
  setupFilter({Arn, InvocationMode::Synchronous, true /*passthrough*/});
  Http::TestRequestHeaderMapImpl headers;
  const auto header_result = filter_->decodeHeaders(headers, true /*end_stream*/);
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, header_result);

  std::string invocation_header_value;
  headers.iterate(
      [](const Http::HeaderEntry& entry, void* ctx) {
        auto* out = static_cast<std::string*>(ctx);
        if (entry.key().getStringView() == "x-amz-invocation-type") {
          out->append(std::string(entry.value().getStringView()));
          return Http::HeaderMap::Iterate::Break;
        }
        return Http::HeaderMap::Iterate::Continue;
      },
      &invocation_header_value);

  EXPECT_EQ("RequestResponse", invocation_header_value);
}

/**
 * A header-only request with pass-through turned off should result in:
 * - a request with JSON body.
 * - content-length header set appropriately
 * - content-type header set to application/json
 * - headers with multiple values coalesced with a comma
 */
TEST_F(AwsLambdaFilterTest, DecodeHeadersOnlyRequestWithJsonOn) {
  using source::extensions::filters::http::aws_lambda::Request;
  setupFilter({Arn, InvocationMode::Synchronous, false /*passthrough*/});
  Buffer::OwnedImpl json_buf;
  auto on_add_decoded_data = [&json_buf](Buffer::Instance& buf, bool) { json_buf.move(buf); };
  ON_CALL(decoder_callbacks_, addDecodedData(_, _)).WillByDefault(Invoke(on_add_decoded_data));
  Http::TestRequestHeaderMapImpl headers;
  headers.setContentLength(0);
  headers.setPath("/resource?proxy=envoy");
  headers.setMethod("GET");
  headers.addCopy("x-custom-header", "unit");
  headers.addCopy("x-custom-header", "test");
  const auto header_result = filter_->decodeHeaders(headers, true /*end_stream*/);

  EXPECT_EQ(Http::FilterHeadersStatus::Continue, header_result);

  // Assert it's not empty
  ASSERT_GT(json_buf.length(), 0);

  ASSERT_NE(headers.ContentType(), nullptr);
  EXPECT_EQ("application/json", headers.ContentType()->value().getStringView());

  // Assert the true (post-transformation) content-length sent to the Lambda endpoint.
  ASSERT_NE(headers.ContentLength(), nullptr);
  EXPECT_EQ(fmt::format("{}", json_buf.length()), headers.ContentLength()->value().getStringView());

  // The best way to verify the generated JSON is to deserialize it and inspect it.
  Request req;
  TestUtility::loadFromJson(json_buf.toString(), req);

  // Assert the content-length wrapped in JSON reflects the original request's value.
  EXPECT_THAT(req.headers(), UnorderedElementsAre(Pair("content-length", "0"),
                                                  Pair("x-custom-header", "unit,test")));
  EXPECT_THAT(req.query_string_parameters(), UnorderedElementsAre(Pair("proxy", "envoy")));
  EXPECT_STREQ("/resource?proxy=envoy", req.raw_path().c_str());
  EXPECT_FALSE(req.is_base64_encoded());
  EXPECT_TRUE(req.body().empty());
  EXPECT_STREQ("GET", req.method().c_str());
}

/**
 * A request with text payload and pass-through turned off should result in:
 * - a request with JSON body containing the original payload
 * - content-length header set appropriately
 * - content-type header set to application/json
 * - headers with multiple values coalesced with a comma
 */
TEST_F(AwsLambdaFilterTest, DecodeDataWithTextualBodyWithJsonOn) {
  using source::extensions::filters::http::aws_lambda::Request;
  setupFilter({Arn, InvocationMode::Synchronous, false /*passthrough*/});

  Buffer::OwnedImpl decoded_buf;
  constexpr absl::string_view expected_plain_text = "Foo bar bazz";
  decoded_buf.add(expected_plain_text);

  auto on_modify_decoding_buffer = [&decoded_buf](std::function<void(Buffer::Instance&)> cb) {
    cb(decoded_buf);
  };
  EXPECT_CALL(decoder_callbacks_, decodingBuffer).WillRepeatedly(Return(&decoded_buf));
  EXPECT_CALL(decoder_callbacks_, modifyDecodingBuffer)
      .WillRepeatedly(Invoke(on_modify_decoding_buffer));

  std::array<const char*, 4> textual_mime_types = {"application/json", "application/javascript",
                                                   "application/xml", "text/plain"};

  for (auto mime_type : textual_mime_types) {
    Http::TestRequestHeaderMapImpl headers;
    headers.setContentLength(expected_plain_text.length());
    headers.setPath("/resource?proxy=envoy");
    headers.setMethod("POST");
    headers.setContentType(mime_type);
    headers.addCopy("x-custom-header", "unit");
    headers.addCopy("x-custom-header", "test");
    const auto header_result = filter_->decodeHeaders(headers, false /*end_stream*/);
    ASSERT_EQ(Http::FilterHeadersStatus::StopIteration, header_result);

    const auto data_result = filter_->decodeData(decoded_buf, true /*end_stream*/);
    ASSERT_EQ(Http::FilterDataStatus::Continue, data_result);

    // Assert decoded buffer is not drained
    ASSERT_GT(decoded_buf.length(), 0);

    ASSERT_NE(headers.ContentType(), nullptr);
    EXPECT_EQ("application/json", headers.ContentType()->value().getStringView());

    // Assert the true (post-transformation) content-length sent to the Lambda endpoint.
    ASSERT_NE(headers.ContentLength(), nullptr);
    EXPECT_EQ(fmt::format("{}", decoded_buf.length()),
              headers.ContentLength()->value().getStringView());

    // The best way to verify the generated JSON is to deserialize it and inspect it.
    Request req;
    TestUtility::loadFromJson(decoded_buf.toString(), req);

    // Assert the content-length wrapped in JSON reflects the original request's value.
    EXPECT_THAT(req.headers(),
                UnorderedElementsAre(
                    Pair("content-length", fmt::format("{}", expected_plain_text.length())),
                    Pair("content-type", mime_type), Pair("x-custom-header", "unit,test")));
    EXPECT_THAT(req.query_string_parameters(), UnorderedElementsAre(Pair("proxy", "envoy")));
    EXPECT_STREQ("/resource?proxy=envoy", req.raw_path().c_str());
    EXPECT_STREQ("POST", req.method().c_str());
    EXPECT_FALSE(req.is_base64_encoded());
    ASSERT_FALSE(req.body().empty());
    EXPECT_STREQ(expected_plain_text.data(), req.body().c_str());

    // reset the buffer for the next iteration
    decoded_buf.drain(decoded_buf.length());
    decoded_buf.add(expected_plain_text);
  }
}

/**
 * A request with binary payload and pass-through turned off should result in a JSON payload with
 * isBase64Encoded flag set.
 * binary payload is determined by looking at both transfer-encoding and content-type.
 */
TEST_F(AwsLambdaFilterTest, DecodeDataWithBinaryBodyWithJsonOn) {
  using source::extensions::filters::http::aws_lambda::Request;
  setupFilter({Arn, InvocationMode::Synchronous, false /*passthrough*/});

  Buffer::OwnedImpl decoded_buf;
  const absl::string_view fake_binary_data = "this should get base64 encoded";
  decoded_buf.add(fake_binary_data);
  EXPECT_CALL(decoder_callbacks_, decodingBuffer).WillRepeatedly(Return(&decoded_buf));
  auto on_modify_decoding_buffer = [&decoded_buf](std::function<void(Buffer::Instance&)> cb) {
    cb(decoded_buf);
  };
  EXPECT_CALL(decoder_callbacks_, modifyDecodingBuffer)
      .WillRepeatedly(Invoke(on_modify_decoding_buffer));
  std::array<absl::string_view, 3> binary_mime_types = {"", "application/pdf", "gzipped"};
  for (auto mime_type : binary_mime_types) {
    Http::TestRequestHeaderMapImpl headers;
    headers.setPath("/");
    headers.setMethod("POST");
    headers.setContentLength(fake_binary_data.length());
    if (mime_type == "gzipped") {
      headers.setTransferEncoding("gzip");
    } else if (!mime_type.empty()) {
      headers.setContentType(mime_type);
    }
    const auto header_result = filter_->decodeHeaders(headers, false /*end_stream*/);
    ASSERT_EQ(Http::FilterHeadersStatus::StopIteration, header_result);

    const auto data_result = filter_->decodeData(decoded_buf, true /*end_stream*/);
    ASSERT_EQ(Http::FilterDataStatus::Continue, data_result);

    // The best way to verify the generated JSON is to deserialize it and inspect it.
    Request req;
    TestUtility::loadFromJson(decoded_buf.toString(), req);

    ASSERT_TRUE(req.is_base64_encoded());
    ASSERT_FALSE(req.body().empty());
    ASSERT_STREQ(req.body().c_str(), "dGhpcyBzaG91bGQgZ2V0IGJhc2U2NCBlbmNvZGVk");

    // reset the buffer for the next iteration
    decoded_buf.drain(decoded_buf.length());
    decoded_buf.add(fake_binary_data);
  }
}

TEST_F(AwsLambdaFilterTest, EncodeHeadersEndStreamShouldSkip) {
  setupFilter({Arn, InvocationMode::Synchronous, true /*passthrough*/});
  Http::TestResponseHeaderMapImpl headers;
  auto result = filter_->encodeHeaders(headers, true /*end_stream*/);
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, result);

  setupFilter({Arn, InvocationMode::Synchronous, false /*passthrough*/});
  result = filter_->encodeHeaders(headers, true /*end_stream*/);
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, result);
}

/**
 * If the Lambda function itself raises an error (syntax, exception, etc.) then we should skip
 * encoding headers and skip the filter.
 */
TEST_F(AwsLambdaFilterTest, EncodeHeadersWithLambdaErrorShouldSkipAndContinue) {
  setupFilter({Arn, InvocationMode::Synchronous, false /*passthrough*/});
  Http::TestResponseHeaderMapImpl headers;
  headers.setStatus(200);
  headers.addCopy(Http::LowerCaseString("x-Amz-Function-Error"), "unhandled");
  auto result = filter_->encodeHeaders(headers, false /*end_stream*/);
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, result);
}

/**
 * If Lambda returns a 5xx error then we should skip encoding headers and skip the filter.
 */
TEST_F(AwsLambdaFilterTest, EncodeHeadersWithLambda5xxShouldSkipAndContinue) {
  setupFilter({Arn, InvocationMode::Synchronous, false /*passthrough*/});
  Http::TestResponseHeaderMapImpl headers;
  headers.setStatus(500);
  auto result = filter_->encodeHeaders(headers, false /*end_stream*/);
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, result);
}

/**
 * encodeHeaders() in a happy path should stop iteration.
 */
TEST_F(AwsLambdaFilterTest, EncodeHeadersStopsIteration) {
  setupFilter({Arn, InvocationMode::Synchronous, false /*passthrough*/});
  Http::TestResponseHeaderMapImpl headers;
  headers.setStatus(200);
  auto result = filter_->encodeHeaders(headers, false /*end_stream*/);
  EXPECT_EQ(Http::FilterHeadersStatus::StopIteration, result);
}

/**
 * encodeData() data in pass-through mode should simply return Continue.
 * This is true whether end_stream is true or false.
 */
TEST_F(AwsLambdaFilterTest, EncodeDataInPassThroughMode) {
  setupFilter({Arn, InvocationMode::Synchronous, true /*passthrough*/});
  Buffer::OwnedImpl buf;
  filter_->resolveSettings();
  auto result = filter_->encodeData(buf, false /*end_stream*/);
  EXPECT_EQ(Http::FilterDataStatus::Continue, result);

  result = filter_->encodeData(buf, true /*end_stream*/);
  EXPECT_EQ(Http::FilterDataStatus::Continue, result);

  setupFilter({Arn, InvocationMode::Asynchronous, true /*passthrough*/});
  filter_->resolveSettings();
  result = filter_->encodeData(buf, false /*end_stream*/);
  EXPECT_EQ(Http::FilterDataStatus::Continue, result);

  result = filter_->encodeData(buf, true /*end_stream*/);
  EXPECT_EQ(Http::FilterDataStatus::Continue, result);
}

/**
 * encodeData() data in asynchronous mode should simply return Continue.
 * This is true whether end_stream is true or false.
 */
TEST_F(AwsLambdaFilterTest, EncodeDataInAsynchrnous) {
  setupFilter({Arn, InvocationMode::Asynchronous, false /*passthrough*/});
  Buffer::OwnedImpl buf;
  filter_->resolveSettings();
  auto result = filter_->encodeData(buf, false /*end_stream*/);
  EXPECT_EQ(Http::FilterDataStatus::Continue, result);

  result = filter_->encodeData(buf, true /*end_stream*/);
  EXPECT_EQ(Http::FilterDataStatus::Continue, result);
}

/**
 * encodeData() data in JSON mode should stop iteration if end_stream is false.
 */
TEST_F(AwsLambdaFilterTest, EncodeDataJsonModeStopIterationAndBuffer) {
  setupFilter({Arn, InvocationMode::Synchronous, false /*passthrough*/});
  Buffer::OwnedImpl buf;
  filter_->resolveSettings();
  auto result = filter_->encodeData(buf, false /*end_stream*/);
  EXPECT_EQ(Http::FilterDataStatus::StopIterationAndBuffer, result);
}

/**
 * encodeData() data in JSON mode without a 'body' key should translate the 'headers' key to HTTP
 * headers while ignoring any HTTP/2 pseudo-headers.
 */
TEST_F(AwsLambdaFilterTest, EncodeDataJsonModeTransformToHttp) {
  setupFilter({Arn, InvocationMode::Synchronous, false /*passthrough*/});
  filter_->resolveSettings();
  Http::TestResponseHeaderMapImpl headers;
  headers.setStatus(200);
  filter_->encodeHeaders(headers, false /*end_stream*/);

  constexpr auto json_response = R"EOF(
  {
      "statusCode": 201,
      "headers": {
                    "x-awesome-header": "awesome value",
                    ":other": "should_never_make_it"
                 },
      "cookies": ["session-id=42; Secure; HttpOnly", "user=joe"]
  }
  )EOF";

  Buffer::OwnedImpl encoded_buf;
  encoded_buf.add(json_response);
  auto on_modify_encoding_buffer = [&encoded_buf](std::function<void(Buffer::Instance&)> cb) {
    cb(encoded_buf);
  };
  EXPECT_CALL(encoder_callbacks_, encodingBuffer).WillRepeatedly(Return(&encoded_buf));
  EXPECT_CALL(encoder_callbacks_, modifyEncodingBuffer)
      .WillRepeatedly(Invoke(on_modify_encoding_buffer));

  auto result = filter_->encodeData(encoded_buf, true /*end_stream*/);
  EXPECT_EQ(Http::FilterDataStatus::Continue, result);

  ASSERT_NE(nullptr, headers.Status());
  EXPECT_EQ("201", headers.Status()->value().getStringView());

  EXPECT_EQ(nullptr, headers.get(Http::LowerCaseString(":other")));

  const auto* custom_header = headers.get(Http::LowerCaseString("x-awesome-header"));
  EXPECT_NE(custom_header, nullptr);
  EXPECT_EQ("awesome value", custom_header->value().getStringView());

  std::vector<std::string> cookies;
  headers.iterate(
      [](const Http::HeaderEntry& entry, void* ctx) {
        auto* list = static_cast<std::vector<std::string>*>(ctx);
        if (entry.key().getStringView() == Http::Headers::get().SetCookie.get()) {
          list->emplace_back(entry.value().getStringView());
        }
        return Http::HeaderMap::Iterate::Continue;
      },
      &cookies);

  EXPECT_THAT(cookies, ElementsAre("session-id=42; Secure; HttpOnly", "user=joe"));
}

/**
 * encodeData() in JSON mode with a non-empty body should translate the body to plain text if it was
 * base64-encoded.
 */
TEST_F(AwsLambdaFilterTest, EncodeDataJsonModeBase64EncodedBody) {
  setupFilter({Arn, InvocationMode::Synchronous, false /*passthrough*/});
  filter_->resolveSettings();
  Http::TestResponseHeaderMapImpl headers;
  headers.setStatus(200);
  filter_->encodeHeaders(headers, false /*end_stream*/);

  constexpr auto json_base64_body = R"EOF(
  {
      "statusCode": 201,
      "body": "Q29mZmVl",
      "isBase64Encoded": true
  }
  )EOF";

  constexpr auto json_plain_text_body = R"EOF(
  {
      "statusCode": 201,
      "body": "Beans",
      "isBase64Encoded": false
  }
  )EOF";

  Buffer::OwnedImpl encoded_buf;
  encoded_buf.add(json_base64_body);
  auto on_modify_encoding_buffer = [&encoded_buf](std::function<void(Buffer::Instance&)> cb) {
    cb(encoded_buf);
  };
  EXPECT_CALL(encoder_callbacks_, encodingBuffer).WillRepeatedly(Return(&encoded_buf));
  EXPECT_CALL(encoder_callbacks_, modifyEncodingBuffer)
      .WillRepeatedly(Invoke(on_modify_encoding_buffer));

  auto result = filter_->encodeData(encoded_buf, true /*end_stream*/);
  EXPECT_EQ(Http::FilterDataStatus::Continue, result);
  EXPECT_STREQ("Coffee", encoded_buf.toString().c_str());

  encoded_buf.drain(encoded_buf.length());

  encoded_buf.add(json_plain_text_body);
  result = filter_->encodeData(encoded_buf, true /*end_stream*/);
  EXPECT_EQ(Http::FilterDataStatus::Continue, result);
  EXPECT_STREQ("Beans", encoded_buf.toString().c_str());
}

/**
 * Encode data in JSON mode _returning_ invalid JSON payload should result in a 500 error.
 */
TEST_F(AwsLambdaFilterTest, EncodeDataJsonModeInvalidJson) {
  setupFilter({Arn, InvocationMode::Synchronous, false /*passthrough*/});
  filter_->resolveSettings();
  Http::TestResponseHeaderMapImpl headers;
  headers.setStatus(200);
  filter_->encodeHeaders(headers, false /*end_stream*/);

  constexpr auto json_response = R"EOF(
  <response>
        <body>Does XML work??</body>
  </response>
  )EOF";

  Buffer::OwnedImpl encoded_buf;
  encoded_buf.add(json_response);
  auto on_modify_encoding_buffer = [&encoded_buf](std::function<void(Buffer::Instance&)> cb) {
    cb(encoded_buf);
  };
  EXPECT_CALL(encoder_callbacks_, encodingBuffer).WillRepeatedly(Return(&encoded_buf));
  EXPECT_CALL(encoder_callbacks_, modifyEncodingBuffer)
      .WillRepeatedly(Invoke(on_modify_encoding_buffer));

  auto result = filter_->encodeData(encoded_buf, true /*end_stream*/);
  EXPECT_EQ(Http::FilterDataStatus::Continue, result);
  EXPECT_EQ(0, encoded_buf.length());

  ASSERT_NE(nullptr, headers.Status());
  EXPECT_EQ("500", headers.Status()->value().getStringView());
}

} // namespace
} // namespace AwsLambdaFilter
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
