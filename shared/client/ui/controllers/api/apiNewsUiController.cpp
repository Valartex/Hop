#include "apiNewsUiController.h"
#include "version.h"

ApiNewsUiController::ApiNewsUiController(NewsModel* newsModel,
                                         NewsController* newsController,
                                         QObject *parent)
    : QObject(parent), m_newsModel(newsModel), m_newsController(newsController)
{
}

void ApiNewsUiController::fetchNews(bool showError)
{
    // В Hop нет новостного раздела провайдера и фоновой загрузки его материалов.
    if (QStringLiteral(APPLICATION_NAME) == QStringLiteral("Hop")) { return; }
    if (!m_newsController) {
        qWarning() << "NewsController is null, skip fetchNews";
        return;
    }

    auto future = m_newsController->fetchNews();
    future.then(this, [this, showError](QPair<ErrorCode, QJsonArray> result) {
        auto [errorCode, newsArray] = result;
        if (errorCode != ErrorCode::NoError) {
            emit errorOccurred(errorCode, showError);
            return;
        }

        m_newsModel->setNewsList(newsArray);
        emit fetchNewsFinished();
    });
}
