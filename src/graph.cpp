#include "graph.h"

#include <QtMultimedia/QAudioDeviceInfo>
#include <QtMultimedia/QAudioInput>

#ifdef HAVE_QT_CHARTS
#include <QtCharts/QChartView>
#include <QtCharts/QLineSeries>
#include <QtCharts/QChart>

QT_CHARTS_USE_NAMESPACE
#endif

#include <QtWidgets/QVBoxLayout>

Graph::Graph(QWidget* parent) :
    QWidget(parent),
    m_chart(nullptr),
    m_series(nullptr)
{
#ifdef HAVE_QT_CHARTS
    m_chart = new QChart;
    m_series = new QLineSeries;

    QChartView* chartView = new QChartView(m_chart);
    chartView->setMinimumSize(500, 300);
    m_chart->addSeries(m_series);
    axisX = new QValueAxis;
    axisX->setLabelFormat("%.02f");
    axisY = new QValueAxis;
    m_chart->addAxis(axisX, Qt::AlignBottom);
    m_series->attachAxis(axisX);
    m_chart->addAxis(axisY, Qt::AlignLeft);
    m_series->attachAxis(axisY);
    m_chart->legend()->hide();

    parent->layout()->addWidget(chartView);
#else
    axisX = nullptr;
    axisY = nullptr;
#endif
}

Graph::~Graph()
{
}

void Graph::addDataPoint(double key, double data)
{
#ifdef HAVE_QT_CHARTS
    m_buffer.push_back(QPointF(key, data));
    if (m_buffer.size() > 3200)
    {
        m_buffer.pop_front();
    }
    m_series->replace(m_buffer);
    axisX->setRange(key-30, key);
#endif
}

void Graph::resetData()
{
#ifdef HAVE_QT_CHARTS
    m_buffer.clear();
    m_series->replace(m_buffer);
#endif
}