package mappers

import (
	"ltm-flight/internal/domain/entities"
	"ltm-flight/internal/infra/api/dto"
)

func NewsToResponse(news entities.News) dto.NewsResponse {
	return dto.NewsResponse{
		NewsID:      news.ID,
		Image:       news.Image,
		Title:       news.Title,
		Description: news.Description,
		Author:      news.Author,
		Content:     news.Content,
		CreatedAt:   news.CreatedAt,
	}
}

func NewsListToResponse(newsList []entities.News) []dto.NewsResponse {
	responses := make([]dto.NewsResponse, len(newsList))
	for i, news := range newsList {
		responses[i] = NewsToResponse(news)
	}
	return responses
}
