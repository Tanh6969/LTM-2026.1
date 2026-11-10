package mappers

import (
	"ltm-flight/internal/domain/entities"
	"ltm-flight/internal/infra/api/dto"
)

func EmailRequestToInput(req dto.MailMessage) entities.Mail {
	return entities.Mail{
		To:      req.To,
		Subject: req.Subject,
		Body:    req.Body,
	}
}
