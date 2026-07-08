package com.example.radargateway;

import org.json.JSONArray;
import org.json.JSONObject;

import java.io.BufferedReader;
import java.io.IOException;
import java.io.InputStream;
import java.io.InputStreamReader;
import java.net.HttpURLConnection;
import java.net.URI;
import java.net.URL;
import java.nio.ByteBuffer;
import java.nio.charset.StandardCharsets;
import java.security.MessageDigest;
import java.text.SimpleDateFormat;
import java.text.ParseException;
import java.util.Date;
import java.util.Locale;
import java.util.TimeZone;

import javax.crypto.Mac;
import javax.crypto.spec.SecretKeySpec;

final class HuaweiIotdaClient {
    private static final String EMPTY_BODY_SHA256 =
            "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855";
    private static final String ALGORITHM = "V11-HMAC-SHA256";

    RadarData fetchShadow() throws Exception {
        String path = "/v5/iot/" + CloudConfig.PROJECT_ID + "/devices/"
                + urlEncode(CloudConfig.DEVICE_ID) + "/shadow";
        URI endpoint = URI.create(stripTrailingSlash(CloudConfig.ENDPOINT) + path);
        String host = endpoint.getHost();
        String sdkDate = huaweiServerTime();

        String signedHeaders = "content-type;host;x-project-id;x-sdk-date";
        String canonicalHeaders = "content-type:application/json\n"
                + "host:" + host + "\n"
                + "x-project-id:" + CloudConfig.PROJECT_ID + "\n"
                + "x-sdk-date:" + sdkDate + "\n";
        String canonicalRequest = "GET\n"
                + canonicalUri(path) + "\n"
                + "\n"
                + canonicalHeaders
                + "\n"
                + signedHeaders + "\n"
                + EMPTY_BODY_SHA256;
        String date = sdkDate.substring(0, 8);
        String info = date + "/" + CloudConfig.REGION_ID + "/" + CloudConfig.DERIVED_AUTH_SERVICE;
        String stringToSign = ALGORITHM + "\n"
                + sdkDate + "\n"
                + info + "\n"
                + sha256Hex(canonicalRequest);
        String derivedKey = hkdfDerivedKeyHex(
                CloudConfig.ACCESS_KEY_ID,
                CloudConfig.SECRET_ACCESS_KEY,
                info
        );
        String signature = hmacHex(derivedKey.getBytes(StandardCharsets.UTF_8), stringToSign);
        String authorization = ALGORITHM
                + " Credential=" + CloudConfig.ACCESS_KEY_ID + "/" + info
                + ", SignedHeaders=" + signedHeaders
                + ", Signature=" + signature;

        HttpURLConnection connection = (HttpURLConnection) new URL(endpoint.toString()).openConnection();
        connection.setRequestMethod("GET");
        connection.setConnectTimeout(12000);
        connection.setReadTimeout(12000);
        connection.setRequestProperty("Accept", "application/json");
        connection.setRequestProperty("Content-Type", "application/json");
        connection.setRequestProperty("X-Project-Id", CloudConfig.PROJECT_ID);
        connection.setRequestProperty("X-Sdk-Date", sdkDate);
        connection.setRequestProperty("Authorization", authorization);

        int code = connection.getResponseCode();
        String body = readAll(code >= 200 && code < 300
                ? connection.getInputStream()
                : connection.getErrorStream());
        connection.disconnect();

        if (code < 200 || code >= 300) {
            throw new IOException("Huawei IoTDA HTTP " + code
                    + "\nX-Sdk-Date: " + sdkDate
                    + "\nSignedHeaders: " + signedHeaders
                    + "\n" + body);
        }
        return parseShadow(body);
    }

    private RadarData parseShadow(String body) throws Exception {
        JSONObject root = new JSONObject(body);
        JSONArray shadow = root.optJSONArray("shadow");
        if (shadow == null) {
            throw new IOException("Response has no shadow array");
        }

        for (int i = 0; i < shadow.length(); i++) {
            JSONObject item = shadow.getJSONObject(i);
            String serviceId = item.optString("service_id", "");
            if (!CloudConfig.SERVICE_ID.equals(serviceId)) {
                continue;
            }
            JSONObject reported = item.optJSONObject("reported");
            JSONObject properties = reported == null ? null : reported.optJSONObject("properties");
            if (properties == null) {
                throw new IOException("Service " + CloudConfig.SERVICE_ID + " has no reported properties");
            }
            int motionLevel = properties.optInt("motion_level", 0);
            String finalResult = properties.optString("final_result", "--");
            int staticTime = properties.optInt("static_time",
                    properties.optInt("static_duration_ms", 0));
            int fallResult = properties.optInt("fall_result", 0);
            int convulsionResult = properties.optInt("convulsion_result", 0);
            int lowObserve = properties.optInt("low_observe", 0);
            int radarOnline = properties.optInt("radar_online", 1);
            String eventTime = reported.optString("event_time", "--");
            return new RadarData(
                    motionLevel,
                    finalResult,
                    staticTime,
                    fallResult,
                    convulsionResult,
                    lowObserve,
                    radarOnline,
                    serviceId,
                    eventTime,
                    utcNow(),
                    "device-shadow:" + serviceId,
                    body
            );
        }
        throw new IOException("Service " + CloudConfig.SERVICE_ID + " not found in device shadow");
    }

    private static String readAll(InputStream inputStream) throws IOException {
        if (inputStream == null) return "";
        StringBuilder builder = new StringBuilder();
        try (BufferedReader reader = new BufferedReader(
                new InputStreamReader(inputStream, StandardCharsets.UTF_8))) {
            String line;
            while ((line = reader.readLine()) != null) {
                builder.append(line);
            }
        }
        return builder.toString();
    }

    private static String stripTrailingSlash(String value) {
        while (value.endsWith("/")) {
            value = value.substring(0, value.length() - 1);
        }
        return value;
    }

    private static String utcNow() {
        SimpleDateFormat format = new SimpleDateFormat("yyyyMMdd'T'HHmmss'Z'", Locale.US);
        format.setTimeZone(TimeZone.getTimeZone("UTC"));
        return format.format(new Date());
    }

    private static String huaweiServerTime() {
        HttpURLConnection connection = null;
        try {
            URL url = new URL(stripTrailingSlash(CloudConfig.ENDPOINT) + "/");
            connection = (HttpURLConnection) url.openConnection();
            connection.setRequestMethod("HEAD");
            connection.setConnectTimeout(5000);
            connection.setReadTimeout(5000);
            connection.getResponseCode();
            String dateHeader = connection.getHeaderField("Date");
            Date serverDate = parseHttpDate(dateHeader);
            if (serverDate != null) {
                return sdkDate(serverDate);
            }
        } catch (Exception ignored) {
            // Falling back to device time is better than failing before the signed request.
        } finally {
            if (connection != null) connection.disconnect();
        }
        return utcNow();
    }

    private static Date parseHttpDate(String value) {
        if (value == null || value.isEmpty()) return null;
        String[] patterns = {
                "EEE, dd MMM yyyy HH:mm:ss zzz",
                "EEEE, dd-MMM-yy HH:mm:ss zzz",
                "EEE MMM d HH:mm:ss yyyy"
        };
        for (String pattern : patterns) {
            try {
                SimpleDateFormat format = new SimpleDateFormat(pattern, Locale.US);
                format.setTimeZone(TimeZone.getTimeZone("UTC"));
                return format.parse(value);
            } catch (ParseException ignored) {
            }
        }
        return null;
    }

    private static String sdkDate(Date date) {
        SimpleDateFormat format = new SimpleDateFormat("yyyyMMdd'T'HHmmss'Z'", Locale.US);
        format.setTimeZone(TimeZone.getTimeZone("UTC"));
        return format.format(date);
    }

    private static String canonicalUri(String path) {
        String[] segments = path.split("/", -1);
        StringBuilder builder = new StringBuilder();
        for (int i = 0; i < segments.length; i++) {
            if (i > 0) builder.append('/');
            builder.append(urlEncode(segments[i]));
        }
        if (builder.length() == 0 || builder.charAt(builder.length() - 1) != '/') {
            builder.append('/');
        }
        return builder.toString();
    }

    private static String urlEncode(String value) {
        byte[] bytes = value.getBytes(StandardCharsets.UTF_8);
        StringBuilder builder = new StringBuilder(bytes.length);
        for (byte raw : bytes) {
            int b = raw & 0xff;
            if ((b >= 'A' && b <= 'Z') || (b >= 'a' && b <= 'z')
                    || (b >= '0' && b <= '9') || b == '-' || b == '.'
                    || b == '_' || b == '~') {
                builder.append((char) b);
            } else {
                builder.append('%');
                String hex = Integer.toHexString(b).toUpperCase(Locale.US);
                if (hex.length() == 1) builder.append('0');
                builder.append(hex);
            }
        }
        return builder.toString();
    }

    private static String hkdfDerivedKeyHex(String ak, String sk, String info) throws Exception {
        byte[] prk = hmacBytes(ak.getBytes(StandardCharsets.UTF_8), sk.getBytes(StandardCharsets.UTF_8));
        ByteBuffer input = ByteBuffer.allocate(info.getBytes(StandardCharsets.UTF_8).length + 1);
        input.put(info.getBytes(StandardCharsets.UTF_8));
        input.put((byte) 0x01);
        byte[] okm = hmacBytes(prk, input.array());
        return hex(okm);
    }

    private static byte[] hmacBytes(byte[] key, byte[] data) throws Exception {
        Mac mac = Mac.getInstance("HmacSHA256");
        mac.init(new SecretKeySpec(key, "HmacSHA256"));
        return mac.doFinal(data);
    }

    private static String hmacHex(byte[] key, String data) throws Exception {
        return hex(hmacBytes(key, data.getBytes(StandardCharsets.UTF_8)));
    }

    private static String sha256Hex(String value) throws Exception {
        MessageDigest digest = MessageDigest.getInstance("SHA-256");
        return hex(digest.digest(value.getBytes(StandardCharsets.UTF_8)));
    }

    private static String hex(byte[] data) {
        StringBuilder builder = new StringBuilder(data.length * 2);
        for (byte b : data) {
            String hex = Integer.toHexString(b & 0xff);
            if (hex.length() == 1) builder.append('0');
            builder.append(hex);
        }
        return builder.toString();
    }
}
