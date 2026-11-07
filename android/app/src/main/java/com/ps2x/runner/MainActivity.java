package com.ps2x.runner;

import android.app.Activity;
import android.content.Intent;
import android.database.Cursor;
import android.graphics.Color;
import android.graphics.Typeface;
import android.graphics.drawable.GradientDrawable;
import android.net.Uri;
import android.os.Bundle;
import android.provider.OpenableColumns;
import android.view.Gravity;
import android.view.View;
import android.view.Window;
import android.view.WindowManager;
import android.widget.Button;
import android.widget.LinearLayout;
import android.widget.ScrollView;
import android.widget.TextView;
import android.widget.Toast;

public final class MainActivity extends Activity {
    private static final int REQUEST_ISO = 4102;
    private Uri selectedIso;
    private TextView selectedFile;
    private Button startButton;

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        getWindow().setStatusBarColor(Color.rgb(12, 10, 11));
        getWindow().setNavigationBarColor(Color.rgb(12, 10, 11));
        getWindow().setFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON,
                WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON);
        buildScreen();
    }

    private int dp(float value) {
        return (int) (value * getResources().getDisplayMetrics().density + 0.5f);
    }

    private GradientDrawable panel(int color, int strokeColor, int radius) {
        GradientDrawable shape = new GradientDrawable();
        shape.setColor(color);
        shape.setCornerRadius(dp(radius));
        if (strokeColor != Color.TRANSPARENT) shape.setStroke(dp(1), strokeColor);
        return shape;
    }

    private TextView label(String value, int size, int color, boolean bold) {
        TextView text = new TextView(this);
        text.setText(value);
        text.setTextSize(size);
        text.setTextColor(color);
        text.setTypeface(Typeface.create(bold ? "sans-serif-condensed" : "sans-serif",
                bold ? Typeface.BOLD : Typeface.NORMAL));
        return text;
    }

    private void buildScreen() {
        ScrollView scroll = new ScrollView(this);
        scroll.setFillViewport(true);
        scroll.setBackgroundColor(Color.rgb(12, 10, 11));

        LinearLayout page = new LinearLayout(this);
        page.setOrientation(LinearLayout.VERTICAL);
        page.setPadding(dp(24), dp(34), dp(24), dp(28));
        page.setGravity(Gravity.CENTER_HORIZONTAL);
        scroll.addView(page);

        TextView overline = label("DARKNESS // SURVIVAL PROJECT", 11,
                Color.rgb(190, 42, 48), true);
        overline.setLetterSpacing(0.22f);
        page.addView(overline, matchWrap());

        TextView title = label("MANHUNT", 48, Color.WHITE, true);
        title.setGravity(Gravity.CENTER);
        title.setLetterSpacing(0.06f);
        LinearLayout.LayoutParams titleParams = matchWrap();
        titleParams.topMargin = dp(18);
        page.addView(title, titleParams);

        TextView sequel = label("II", 34, Color.rgb(205, 35, 43), true);
        sequel.setGravity(Gravity.CENTER);
        page.addView(sequel, matchWrap());

        View rule = new View(this);
        rule.setBackgroundColor(Color.rgb(150, 24, 31));
        LinearLayout.LayoutParams ruleParams = new LinearLayout.LayoutParams(dp(72), dp(3));
        ruleParams.topMargin = dp(22);
        ruleParams.bottomMargin = dp(26);
        page.addView(rule, ruleParams);

        TextView intro = label("SELECCIONAR IMAGEN DEL JUEGO", 17,
                Color.WHITE, true);
        intro.setGravity(Gravity.CENTER);
        page.addView(intro, matchWrap());

        TextView hint = label("CARGA UN ARCHIVO ISO DESDE TU DISPOSITIVO", 11,
                Color.rgb(150, 143, 143), false);
        hint.setGravity(Gravity.CENTER);
        hint.setLetterSpacing(0.08f);
        LinearLayout.LayoutParams hintParams = matchWrap();
        hintParams.topMargin = dp(8);
        page.addView(hint, hintParams);

        LinearLayout fileCard = new LinearLayout(this);
        fileCard.setOrientation(LinearLayout.VERTICAL);
        fileCard.setPadding(dp(18), dp(18), dp(18), dp(18));
        fileCard.setBackground(panel(Color.rgb(25, 21, 22), Color.rgb(91, 35, 39), 8));
        LinearLayout.LayoutParams cardParams = new LinearLayout.LayoutParams(
                LinearLayout.LayoutParams.MATCH_PARENT, LinearLayout.LayoutParams.WRAP_CONTENT);
        cardParams.topMargin = dp(30);
        page.addView(fileCard, cardParams);

        TextView fileCaption = label("ARCHIVO SELECCIONADO", 10,
                Color.rgb(194, 48, 54), true);
        fileCaption.setLetterSpacing(0.12f);
        fileCard.addView(fileCaption, matchWrap());

        selectedFile = label("NINGÚN ARCHIVO CARGADO", 14,
                Color.rgb(218, 211, 211), true);
        selectedFile.setMaxLines(3);
        selectedFile.setPadding(0, dp(12), 0, dp(4));
        fileCard.addView(selectedFile, matchWrap());

        Button chooseButton = makeButton("EXAMINAR ARCHIVOS", true);
        LinearLayout.LayoutParams chooseParams = new LinearLayout.LayoutParams(
                LinearLayout.LayoutParams.MATCH_PARENT, dp(54));
        chooseParams.topMargin = dp(22);
        page.addView(chooseButton, chooseParams);
        chooseButton.setOnClickListener(v -> openIsoPicker());

        startButton = makeButton("INICIAR", false);
        startButton.setEnabled(false);
        startButton.setAlpha(0.48f);
        LinearLayout.LayoutParams startParams = new LinearLayout.LayoutParams(
                LinearLayout.LayoutParams.MATCH_PARENT, dp(54));
        startParams.topMargin = dp(12);
        page.addView(startButton, startParams);
        startButton.setOnClickListener(v -> showRuntimeNotice());

        TextView footer = label("NO ONE GETS OUT CLEAN.", 11,
                Color.rgb(112, 104, 105), true);
        footer.setGravity(Gravity.CENTER);
        footer.setLetterSpacing(0.18f);
        LinearLayout.LayoutParams footerParams = matchWrap();
        footerParams.topMargin = dp(34);
        page.addView(footer, footerParams);

        setContentView(scroll);
    }

    private LinearLayout.LayoutParams matchWrap() {
        return new LinearLayout.LayoutParams(
                LinearLayout.LayoutParams.MATCH_PARENT,
                LinearLayout.LayoutParams.WRAP_CONTENT);
    }

    private Button makeButton(String text, boolean primary) {
        Button button = new Button(this);
        button.setText(text);
        button.setTextSize(14);
        button.setTypeface(Typeface.create("sans-serif-condensed", Typeface.BOLD));
        button.setLetterSpacing(0.08f);
        button.setTextColor(Color.WHITE);
        button.setAllCaps(false);
        button.setBackground(panel(primary ? Color.rgb(139, 22, 30) :
                Color.rgb(48, 22, 25), Color.rgb(190, 43, 51), 6));
        return button;
    }

    private void openIsoPicker() {
        Intent intent = new Intent(Intent.ACTION_OPEN_DOCUMENT);
        intent.addCategory(Intent.CATEGORY_OPENABLE);
        intent.setType("*/*");
        intent.putExtra(Intent.EXTRA_MIME_TYPES, new String[] {
                "application/octet-stream", "application/x-iso9660-image"
        });
        intent.addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION |
                Intent.FLAG_GRANT_PERSISTABLE_URI_PERMISSION);
        startActivityForResult(intent, REQUEST_ISO);
    }

    @Override
    protected void onActivityResult(int requestCode, int resultCode, Intent data) {
        super.onActivityResult(requestCode, resultCode, data);
        if (requestCode != REQUEST_ISO || resultCode != RESULT_OK ||
                data == null || data.getData() == null) return;

        selectedIso = data.getData();
        try {
            getContentResolver().takePersistableUriPermission(selectedIso,
                    Intent.FLAG_GRANT_READ_URI_PERMISSION);
        } catch (SecurityException ignored) {
            // Some document providers do not offer persistable permissions.
        }

        String name = queryDisplayName(selectedIso);
        selectedFile.setText(name);
        startButton.setEnabled(true);
        startButton.setAlpha(1.0f);
        Toast.makeText(this, "ISO seleccionada", Toast.LENGTH_SHORT).show();
    }

    private String queryDisplayName(Uri uri) {
        Cursor cursor = null;
        try {
            cursor = getContentResolver().query(uri,
                    new String[] { OpenableColumns.DISPLAY_NAME }, null, null, null);
            if (cursor != null && cursor.moveToFirst()) {
                int index = cursor.getColumnIndex(OpenableColumns.DISPLAY_NAME);
                if (index >= 0) return cursor.getString(index);
            }
        } catch (Exception ignored) {
            // Fall back to the URI name when the provider has no display-name column.
        } finally {
            if (cursor != null) cursor.close();
        }
        String name = uri.getLastPathSegment();
        return name == null ? "ISO SELECCIONADA" : name;
    }

    private void showRuntimeNotice() {
        Toast.makeText(this,
                "Selector listo. El runtime actual necesita un ELF recompilado; aún no arranca ISO directamente.",
                Toast.LENGTH_LONG).show();
    }
}
