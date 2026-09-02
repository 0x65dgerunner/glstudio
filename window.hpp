#pragma once

#include <edgeqt/window.hpp>

class ModelViewport;

class GlStudioWindow : public edgeqt::Window {
    Q_OBJECT

public:
    explicit GlStudioWindow(QWidget* parent = nullptr);

private:
    ModelViewport* viewport_ = nullptr;
};
