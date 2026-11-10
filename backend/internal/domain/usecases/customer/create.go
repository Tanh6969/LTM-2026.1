package customer

import (
	"context"
	"database/sql"
	"errors"

	"ltm-flight/internal/domain/adapters"
	"ltm-flight/internal/domain/entities"
)

type ICreateCustomerUseCase interface {
	Execute(ctx context.Context, customer entities.CreateUserParams) (entities.User, error)
}

type CreateCustomerUseCase struct {
	userRepository     adapters.IUserRepository
	customerRepository adapters.ICustomerRepository
}

func NewCreateCustomerUseCase(customerRepository adapters.ICustomerRepository, userRepository adapters.IUserRepository) ICreateCustomerUseCase {
	return &CreateCustomerUseCase{
		customerRepository: customerRepository,
		userRepository:     userRepository,
	}
}
func (c *CreateCustomerUseCase) Execute(ctx context.Context, customer entities.CreateUserParams) (entities.User, error) {
	existingUser, err := c.userRepository.GetUserByEmail(ctx, customer.Email)
	if err != nil && !errors.Is(err, sql.ErrNoRows) {
		return entities.User{}, errors.New("failed to check existing user")
	}
	if existingUser != nil {
		return entities.User{}, adapters.ErrEmailInUse
	}

	createdCustomer, err := c.customerRepository.CreateCustomerTx(ctx, customer)
	if err != nil {
		return entities.User{}, err
	}

	return createdCustomer, nil
}
