package com.wmmt.webview;

import android.app.Activity;
import android.content.Intent;
import android.net.Uri;
import android.os.Build;
import android.os.Bundle;
import android.view.View;
import android.view.ViewGroup;
import android.view.WindowManager;
import android.webkit.WebChromeClient;
import android.webkit.WebSettings;
import android.webkit.WebView;
import android.webkit.WebViewClient;
import android.widget.FrameLayout;

/** A thin Android shell for the public wmmtd.xyz browser port. No game data is bundled. */
public class MainActivity extends Activity {
    private static final String HOME = "https://wmmtd.xyz/";

    private WebView web;
    private View customView;
    private FrameLayout fullscreenContainer;
    private WebChromeClient.CustomViewCallback customViewCallback;

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        getWindow().addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON);

        web = new WebView(this);
        setContentView(web);

        WebSettings s = web.getSettings();
        s.setJavaScriptEnabled(true);
        s.setDomStorageEnabled(true);
        s.setDatabaseEnabled(true);
        s.setLoadWithOverviewMode(true);
        s.setUseWideViewPort(true);
        s.setSupportZoom(false);
        s.setBuiltInZoomControls(false);
        s.setDisplayZoomControls(false);
        s.setMediaPlaybackRequiresUserGesture(false);
        s.setCacheMode(WebSettings.LOAD_DEFAULT);
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.LOLLIPOP) {
            s.setMixedContentMode(WebSettings.MIXED_CONTENT_COMPATIBILITY_MODE);
        }
        WebView.setWebContentsDebuggingEnabled(false);

        web.setWebViewClient(new WebViewClient() {
            @Override
            public boolean shouldOverrideUrlLoading(WebView v, String url) {
                if (url.startsWith("https://wmmtd.xyz/") || url.startsWith("http://wmmtd.xyz/")) {
                    return false;
                }
                try {
                    startActivity(new Intent(Intent.ACTION_VIEW, Uri.parse(url)));
                } catch (Exception ignored) {
                }
                return true;
            }
        });

        web.setWebChromeClient(new WebChromeClient() {
            @Override
            public void onShowCustomView(View view, CustomViewCallback callback) {
                if (customView != null) {
                    callback.onCustomViewHidden();
                    return;
                }
                customView = view;
                customViewCallback = callback;
                fullscreenContainer = new FrameLayout(MainActivity.this);
                fullscreenContainer.setBackgroundColor(0xff000000);
                fullscreenContainer.addView(view, new FrameLayout.LayoutParams(
                        FrameLayout.LayoutParams.MATCH_PARENT, FrameLayout.LayoutParams.MATCH_PARENT));
                addContentView(fullscreenContainer, new FrameLayout.LayoutParams(
                        FrameLayout.LayoutParams.MATCH_PARENT, FrameLayout.LayoutParams.MATCH_PARENT));
                web.setVisibility(View.GONE);
            }

            @Override
            public void onHideCustomView() {
                if (customView == null) {
                    return;
                }
                ViewGroup parent = (ViewGroup) fullscreenContainer.getParent();
                if (parent != null) {
                    parent.removeView(fullscreenContainer);
                }
                web.setVisibility(View.VISIBLE);
                customView = null;
                fullscreenContainer = null;
                if (customViewCallback != null) {
                    customViewCallback.onCustomViewHidden();
                }
                customViewCallback = null;
                hideUi();
            }
        });

        web.loadUrl(HOME);
        hideUi();
    }

    @Override
    public void onWindowFocusChanged(boolean hasFocus) {
        super.onWindowFocusChanged(hasFocus);
        if (hasFocus) {
            hideUi();
        }
    }

    @Override
    public void onBackPressed() {
        if (customView != null) {
            hideFullscreen();
        } else if (web.canGoBack()) {
            web.goBack();
        } else {
            super.onBackPressed();
        }
    }

    private void hideFullscreen() {
        if (customViewCallback != null) {
            customViewCallback.onCustomViewHidden();
        }
        ViewGroup parent = fullscreenContainer == null ? null : (ViewGroup) fullscreenContainer.getParent();
        if (parent != null) {
            parent.removeView(fullscreenContainer);
        }
        web.setVisibility(View.VISIBLE);
        customView = null;
        fullscreenContainer = null;
        customViewCallback = null;
        hideUi();
    }

    private void hideUi() {
        getWindow().getDecorView().setSystemUiVisibility(
                View.SYSTEM_UI_FLAG_LAYOUT_STABLE
                        | View.SYSTEM_UI_FLAG_LAYOUT_HIDE_NAVIGATION
                        | View.SYSTEM_UI_FLAG_LAYOUT_FULLSCREEN
                        | View.SYSTEM_UI_FLAG_HIDE_NAVIGATION
                        | View.SYSTEM_UI_FLAG_FULLSCREEN
                        | View.SYSTEM_UI_FLAG_IMMERSIVE_STICKY);
    }
}
