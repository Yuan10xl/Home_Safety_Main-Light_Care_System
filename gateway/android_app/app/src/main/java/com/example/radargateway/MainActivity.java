package com.example.radargateway;

import android.app.Activity;
import android.graphics.Color;
import android.graphics.Typeface;
import android.os.Bundle;
import android.os.Handler;
import android.os.Looper;
import android.text.TextUtils;
import android.view.Gravity;
import android.view.ViewGroup;
import android.widget.Button;
import android.widget.LinearLayout;
import android.widget.ProgressBar;
import android.widget.ScrollView;
import android.widget.TextView;

import java.util.Locale;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;

public final class MainActivity extends Activity {
    private final Handler handler = new Handler(Looper.getMainLooper());
    private final ExecutorService executor = Executors.newSingleThreadExecutor();
    private final HuaweiIotdaClient client = new HuaweiIotdaClient();

    private TextView statusText;
    private TextView motionText;
    private TextView resultText;
    private TextView serviceText;
    private TextView sampleText;
    private TextView eventTimeText;
    private TextView topicText;
    private TextView staticTimeText;
    private TextView fallText;
    private TextView convulsionText;
    private TextView lowObserveText;
    private TextView radarOnlineText;
    private TextView rawText;
    private ProgressBar motionBar;
    private TrendView trendView;

    private boolean running = true;
    private boolean fetching = false;
    private int sampleCount = 0;

    private final Runnable pollTask = new Runnable() {
        @Override
        public void run() {
            fetchNow();
            if (running) {
                handler.postDelayed(this, CloudConfig.POLL_INTERVAL_MS);
            }
        }
    };

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        setContentView(createContent());
        handler.post(pollTask);
    }

    @Override
    protected void onDestroy() {
        running = false;
        executor.shutdownNow();
        super.onDestroy();
    }

    private ScrollView createContent() {
        ScrollView scrollView = new ScrollView(this);
        scrollView.setFillViewport(true);
        scrollView.setBackgroundColor(Color.rgb(245, 247, 251));

        LinearLayout root = new LinearLayout(this);
        root.setOrientation(LinearLayout.VERTICAL);
        root.setPadding(dp(18), dp(20), dp(18), dp(24));
        scrollView.addView(root, new ScrollView.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT,
                ViewGroup.LayoutParams.WRAP_CONTENT
        ));

        TextView title = text("雷达照护数据看板", 30, Color.rgb(23, 32, 51), true);
        root.addView(title);
        TextView subtitle = text("Huawei IoTDA / GatewayCare", 13, Color.rgb(100, 116, 139), true);
        subtitle.setPadding(0, dp(4), 0, dp(14));
        root.addView(subtitle);

        statusText = pill("等待云端数据");
        root.addView(statusText);

        LinearLayout row1 = row();
        root.addView(row1);
        LinearLayout motionCard = card();
        LinearLayout resultCard = card();
        row1.addView(motionCard, weightParams());
        row1.addView(resultCard, weightParams());

        motionCard.addView(label("活动强度"));
        motionText = value("--");
        motionCard.addView(motionText);
        motionBar = new ProgressBar(this, null, android.R.attr.progressBarStyleHorizontal);
        motionBar.setMax(100);
        motionCard.addView(motionBar, new LinearLayout.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT,
                dp(10)
        ));

        resultCard.addView(label("识别结果"));
        resultText = value("--");
        resultCard.addView(resultText);
        resultCard.addView(small("尚未收到属性上报"));

        LinearLayout row2 = row();
        root.addView(row2);
        LinearLayout serviceCard = card();
        LinearLayout sampleCard = card();
        row2.addView(serviceCard, weightParams());
        row2.addView(sampleCard, weightParams());

        serviceCard.addView(label("最新服务"));
        serviceText = value("--");
        serviceCard.addView(serviceText);
        topicText = small("--");
        serviceCard.addView(topicText);

        sampleCard.addView(label("样本数"));
        sampleText = value("0");
        sampleCard.addView(sampleText);
        eventTimeText = small("未更新");
        sampleCard.addView(eventTimeText);

        LinearLayout detailCard = card();
        root.addView(detailCard);
        detailCard.addView(label("低活动与风险详情"));
        staticTimeText = small("低活动持续：--");
        fallText = small("跌倒提示：--");
        convulsionText = small("抽搐样风险特征提示：--");
        lowObserveText = small("低活动观察：--");
        radarOnlineText = small("雷达板：--");
        detailCard.addView(staticTimeText);
        detailCard.addView(fallText);
        detailCard.addView(convulsionText);
        detailCard.addView(lowObserveText);
        detailCard.addView(radarOnlineText);

        LinearLayout trendCard = card();
        root.addView(trendCard);
        trendCard.addView(label("活动强度趋势"));
        trendView = new TrendView(this);
        trendCard.addView(trendView, new LinearLayout.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT,
                dp(220)
        ));

        LinearLayout rawCard = card();
        root.addView(rawCard);
        rawCard.addView(label("最近原始消息"));
        rawText = small("暂无数据");
        rawText.setSingleLine(false);
        rawText.setEllipsize(null);
        rawCard.addView(rawText);

        Button refresh = new Button(this);
        refresh.setText("立即刷新");
        refresh.setOnClickListener(v -> fetchNow());
        root.addView(refresh, new LinearLayout.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT,
                dp(52)
        ));

        return scrollView;
    }

    private void fetchNow() {
        if (fetching) {
            return;
        }
        fetching = true;
        setStatus("正在连接华为云...", false);
        executor.execute(() -> {
            try {
                RadarData data = client.fetchShadow();
                handler.post(() -> applyData(data));
            } catch (Exception error) {
                handler.post(() -> showError(error));
            }
        });
    }

    private void applyData(RadarData data) {
        fetching = false;
        sampleCount += 1;
        int normalized = Math.max(0, Math.min(100, data.motionLevel));
        motionText.setText(String.valueOf(data.motionLevel));
        motionBar.setProgress(normalized);
        resultText.setText(resultLabel(data.finalResult));
        serviceText.setText(data.serviceId);
        topicText.setText(data.topic);
        sampleText.setText(String.format(Locale.US, "%d", sampleCount));
        eventTimeText.setText("云端时间 " + formatCloudTime(data.eventTime));
        staticTimeText.setText("低活动持续：" + formatDurationMs(data.staticTime));
        fallText.setText("跌倒提示：" + flagText(data.fallResult, "疑似跌倒", "无"));
        convulsionText.setText("抽搐样风险特征提示：" + flagText(data.convulsionResult, "有风险特征", "无"));
        lowObserveText.setText("低活动观察：" + flagText(data.lowObserve, "观察中", "否"));
        radarOnlineText.setText("雷达板：" + flagText(data.radarOnline, "在线", "离线"));
        rawText.setText(compact(data.rawJson));
        trendView.addValue(normalized);
        setStatus("云端连接正常", true);
    }

    private void showError(Exception error) {
        fetching = false;
        setStatus("连接异常", false);
        rawText.setText(error.getMessage() == null ? error.toString() : error.getMessage());
    }

    private void setStatus(String text, boolean ok) {
        statusText.setText(text);
        statusText.setTextColor(ok ? Color.rgb(22, 101, 52) : Color.rgb(146, 64, 14));
        statusText.setBackgroundColor(ok ? Color.rgb(220, 252, 231) : Color.rgb(254, 243, 199));
    }

    private LinearLayout row() {
        LinearLayout row = new LinearLayout(this);
        row.setOrientation(LinearLayout.HORIZONTAL);
        row.setPadding(0, 0, 0, dp(12));
        row.setLayoutParams(new LinearLayout.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT,
                ViewGroup.LayoutParams.WRAP_CONTENT
        ));
        return row;
    }

    private LinearLayout card() {
        LinearLayout card = new LinearLayout(this);
        card.setOrientation(LinearLayout.VERTICAL);
        card.setPadding(dp(16), dp(14), dp(16), dp(14));
        card.setBackgroundColor(Color.WHITE);
        LinearLayout.LayoutParams params = new LinearLayout.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT,
                ViewGroup.LayoutParams.WRAP_CONTENT
        );
        params.setMargins(0, 0, 0, dp(12));
        card.setLayoutParams(params);
        return card;
    }

    private LinearLayout.LayoutParams weightParams() {
        LinearLayout.LayoutParams params = new LinearLayout.LayoutParams(
                0,
                ViewGroup.LayoutParams.WRAP_CONTENT,
                1f
        );
        params.setMargins(0, 0, dp(10), 0);
        return params;
    }

    private TextView text(String content, int sp, int color, boolean bold) {
        TextView view = new TextView(this);
        view.setText(content);
        view.setTextSize(sp);
        view.setTextColor(color);
        view.setIncludeFontPadding(true);
        if (bold) view.setTypeface(Typeface.DEFAULT, Typeface.BOLD);
        return view;
    }

    private TextView label(String content) {
        TextView view = text(content, 15, Color.rgb(71, 85, 105), true);
        view.setPadding(0, 0, 0, dp(8));
        return view;
    }

    private TextView value(String content) {
        TextView view = text(content, 32, Color.rgb(15, 23, 42), true);
        view.setSingleLine(true);
        view.setEllipsize(TextUtils.TruncateAt.END);
        view.setPadding(0, 0, 0, dp(8));
        return view;
    }

    private TextView small(String content) {
        TextView view = text(content, 13, Color.rgb(100, 116, 139), false);
        view.setLineSpacing(0f, 1.15f);
        return view;
    }

    private TextView pill(String content) {
        TextView view = text(content, 14, Color.rgb(146, 64, 14), true);
        view.setGravity(Gravity.CENTER);
        view.setPadding(dp(14), dp(8), dp(14), dp(8));
        view.setBackgroundColor(Color.rgb(254, 243, 199));
        return view;
    }

    private String compact(String raw) {
        if (raw == null) return "";
        return raw.length() > 1600 ? raw.substring(0, 1600) + "\n..." : raw;
    }

    private String resultLabel(String value) {
        if (value == null) return "--";
        String lowered = value.toLowerCase(Locale.US);
        if ("normal".equals(lowered) || "ok".equals(lowered)) {
            return "正常";
        }
        if (lowered.contains("low_activity") || lowered.contains("observe")) {
            return "低活动观察中";
        }
        if (lowered.contains("fall")) {
            return "疑似跌倒";
        }
        if (lowered.contains("convulsion") || lowered.contains("abnormal")) {
            return "疑似异常活动波动";
        }
        if (lowered.contains("confirm")) {
            return "请家属确认";
        }
        return value;
    }

    private String flagText(int value, String positive, String negative) {
        return value != 0 ? positive : negative;
    }

    private String formatDurationMs(int value) {
        if (value < 1000) {
            return value + " ms";
        }
        return String.format(Locale.US, "%.1f 秒", value / 1000.0);
    }

    private String formatCloudTime(String value) {
        if (value == null || value.length() < 15) {
            return "--";
        }
        return value.substring(0, 4) + "-" + value.substring(4, 6) + "-" + value.substring(6, 8)
                + " " + value.substring(9, 11) + ":" + value.substring(11, 13) + ":" + value.substring(13, 15)
                + " UTC";
    }

    private int dp(int value) {
        return Math.round(value * getResources().getDisplayMetrics().density);
    }
}
