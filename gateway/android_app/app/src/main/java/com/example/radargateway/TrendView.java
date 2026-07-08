package com.example.radargateway;

import android.content.Context;
import android.graphics.Canvas;
import android.graphics.Color;
import android.graphics.Paint;
import android.util.AttributeSet;
import android.view.View;

import java.util.ArrayList;
import java.util.List;

public final class TrendView extends View {
    private final Paint gridPaint = new Paint(Paint.ANTI_ALIAS_FLAG);
    private final Paint linePaint = new Paint(Paint.ANTI_ALIAS_FLAG);
    private final Paint pointPaint = new Paint(Paint.ANTI_ALIAS_FLAG);
    private final List<Integer> values = new ArrayList<>();

    public TrendView(Context context) {
        super(context);
        init();
    }

    public TrendView(Context context, AttributeSet attrs) {
        super(context, attrs);
        init();
    }

    private void init() {
        gridPaint.setColor(Color.rgb(226, 232, 240));
        gridPaint.setStrokeWidth(2f);
        linePaint.setColor(Color.rgb(79, 111, 222));
        linePaint.setStrokeWidth(6f);
        linePaint.setStyle(Paint.Style.STROKE);
        pointPaint.setColor(Color.rgb(26, 188, 156));
        pointPaint.setStyle(Paint.Style.FILL);
    }

    public void addValue(int value) {
        values.add(Math.max(0, Math.min(100, value)));
        while (values.size() > 60) {
            values.remove(0);
        }
        invalidate();
    }

    @Override
    protected void onDraw(Canvas canvas) {
        super.onDraw(canvas);
        int width = getWidth();
        int height = getHeight();
        int left = 24;
        int right = width - 24;
        int top = 24;
        int bottom = height - 24;

        for (int i = 0; i <= 4; i++) {
            float y = top + (bottom - top) * i / 4f;
            canvas.drawLine(left, y, right, y, gridPaint);
        }

        if (values.isEmpty()) {
            return;
        }

        float step = values.size() == 1 ? 0 : (right - left) / (float) (values.size() - 1);
        float prevX = left;
        float prevY = valueToY(values.get(0), top, bottom);
        canvas.drawCircle(prevX, prevY, 7f, pointPaint);
        for (int i = 1; i < values.size(); i++) {
            float x = left + step * i;
            float y = valueToY(values.get(i), top, bottom);
            canvas.drawLine(prevX, prevY, x, y, linePaint);
            canvas.drawCircle(x, y, 7f, pointPaint);
            prevX = x;
            prevY = y;
        }
    }

    private float valueToY(int value, int top, int bottom) {
        return bottom - (bottom - top) * (value / 100f);
    }
}
